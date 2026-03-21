#include <switch.h>
#include <switch/services/spsm.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <netdb.h>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

struct Config {
  std::string server_url = "http://10.0.0.151:8080";
  std::string api_key = "change-me";
  std::string save_dir = "sdmc:/mGBA";
  std::string rom_dir = "sdmc:/mGBA";
  std::string rom_extension = ".gba";
  std::set<std::string> locked_ids;
};

struct SaveMeta {
  std::string game_id;
  std::string last_modified_utc;
  std::string server_updated_at;
  std::string sha256;
  std::string filename_hint;
};

struct LocalSave {
  std::string path;
  std::string name;
  std::string game_id;
  std::string last_modified_utc;
  std::string sha256;
  size_t size_bytes = 0;
  bool mtime_trusted = false;
  std::int64_t st_mtime_unix = -1;
};

struct BaselineRow {
  std::string game_id;
  std::string sha256;
};

struct IdMapRow {
  std::string save_stem;
  std::string game_id;
};

enum class SyncAction {
  Auto,
  UploadOnly,
  DownloadOnly,
  DropboxSync,
  SaveViewer,
};

struct SyncManualFilter {
  bool all = true;
  std::set<std::string> ids;
};

static std::string trim(const std::string& input) {
  const auto start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const auto end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
}

static std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

static Config load_config(const std::string& path) {
  Config cfg;
  std::ifstream file(path);
  if (!file.good()) return cfg;

  std::string section;
  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      continue;
    }
    const auto split = line.find('=');
    if (split == std::string::npos) continue;
    const std::string key = trim(line.substr(0, split));
    const std::string value = trim(line.substr(split + 1));
    if (section == "server" && key == "url") cfg.server_url = value;
    if (section == "server" && key == "api_key") cfg.api_key = value;
    if (section == "sync" && key == "save_dir") cfg.save_dir = value;
    if (section == "sync" && key == "locked_ids") {
      std::stringstream ss(value);
      std::string tok;
      while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        if (!tok.empty()) cfg.locked_ids.insert(to_lower(tok));
      }
    }
    if (section == "rom" && key == "rom_dir") cfg.rom_dir = value;
    if (section == "rom" && key == "rom_extension") cfg.rom_extension = value;
  }
  return cfg;
}

static std::string sanitize_game_id(const std::string& stem) {
  std::string out;
  for (char c : to_lower(stem)) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.') {
      out.push_back(c);
    } else {
      out.push_back('-');
    }
  }
  while (!out.empty() && out.front() == '-') out.erase(out.begin());
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out.empty() ? "unknown-game" : out;
}

static bool has_sav_extension(const std::string& name) {
  if (name.size() < 4) return false;
  return to_lower(name.substr(name.size() - 4)) == ".sav";
}

static std::string file_stem(const std::string& name) {
  const auto dot = name.find_last_of('.');
  return (dot == std::string::npos) ? name : name.substr(0, dot);
}

static std::vector<unsigned char> read_file(const std::string& path);

static std::string decode_header_field(const unsigned char* start, size_t len) {
  std::string out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    char c = static_cast<char>(start[i]);
    if (c == '\0') break;
    if (std::isprint(static_cast<unsigned char>(c))) out.push_back(c);
  }
  return trim(out);
}

static std::string game_id_from_rom_header(const std::vector<unsigned char>& rom_data) {
  if (rom_data.size() < 0xB0) return "";
  const std::string title = decode_header_field(rom_data.data() + 0xA0, 12);
  const std::string code = decode_header_field(rom_data.data() + 0xAC, 4);
  if (title.empty() && code.empty()) return "";
  const std::string combined = code.empty() ? title : (title + "-" + code);
  return sanitize_game_id(combined);
}

/* ROM title/gamecode live in the first ~0xB0 bytes; avoid reading multi‑MiB ROMs per save. */
static std::vector<unsigned char> read_file_prefix(const std::string& path, size_t max_bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file.good()) return {};
  std::vector<unsigned char> out(max_bytes);
  file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(max_bytes));
  out.resize(static_cast<size_t>(file.gcount()));
  return out;
}

static std::string resolve_game_id_for_save(const Config& cfg, const std::string& save_name) {
  const std::string stem = file_stem(save_name);
  if (!cfg.rom_dir.empty()) {
    std::string ext = cfg.rom_extension.empty() ? ".gba" : cfg.rom_extension;
    if (!ext.empty() && ext[0] != '.') ext = "." + ext;
    const std::string rom_path = cfg.rom_dir + "/" + stem + ext;
    const std::vector<unsigned char> rom_hdr = read_file_prefix(rom_path, 512);
    if (rom_hdr.size() >= 0xB0) {
      const std::string from_header = game_id_from_rom_header(rom_hdr);
      if (!from_header.empty()) return from_header;
    }
  }
  return sanitize_game_id(stem);
}

static std::vector<unsigned char> read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.good()) return {};
  file.seekg(0, std::ios::end);
  size_t len = static_cast<size_t>(file.tellg());
  file.seekg(0, std::ios::beg);
  std::vector<unsigned char> out(len);
  if (len) file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(len));
  return out;
}

static bool write_atomic(const std::string& path, const std::vector<unsigned char>& data) {
  std::string tmp = path + ".tmp";
  std::ofstream file(tmp, std::ios::binary);
  if (!file.good()) return false;
  if (!data.empty()) file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  file.close();
  if (std::rename(tmp.c_str(), path.c_str()) == 0) return true;
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) == 0) return true;

  // Fallback path for filesystems/locks where rename replacement fails.
  std::ofstream direct(path, std::ios::binary);
  if (!direct.good()) return false;
  if (!data.empty()) direct.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  const bool ok = direct.good();
  direct.close();
  std::remove(tmp.c_str());
  return ok;
}

static std::string sanitize_filename(const std::string& input, const std::string& fallback) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '/' || c == '\\' || c == ':') continue;
    out.push_back(c);
  }
  if (out.empty()) return fallback;
  return out;
}

namespace sha256_impl {
static constexpr uint32_t K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};
static inline uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
static std::string hash(const std::vector<unsigned char>& data) {
  uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8U;
  std::vector<unsigned char> msg = data;
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56) msg.push_back(0x00);
  for (int i = 7; i >= 0; --i) msg.push_back(static_cast<unsigned char>((bit_len >> (i * 8)) & 0xffU));
  uint32_t h0 = 0x6a09e667U, h1 = 0xbb67ae85U, h2 = 0x3c6ef372U, h3 = 0xa54ff53aU;
  uint32_t h4 = 0x510e527fU, h5 = 0x9b05688cU, h6 = 0x1f83d9abU, h7 = 0x5be0cd19U;
  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[64] = {0};
    for (int i = 0; i < 16; ++i) {
      size_t j = chunk + static_cast<size_t>(i) * 4;
      w[i] = (uint32_t(msg[j]) << 24) | (uint32_t(msg[j + 1]) << 16) | (uint32_t(msg[j + 2]) << 8) | uint32_t(msg[j + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;
    for (int i = 0; i < 64; ++i) {
      uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + s1 + ch + K[i] + w[i];
      uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    h0 += a; h1 += b; h2 += c; h3 += d; h4 += e; h5 += f; h6 += g; h7 += h;
  }
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  out << std::setw(8) << h0 << std::setw(8) << h1 << std::setw(8) << h2 << std::setw(8) << h3
      << std::setw(8) << h4 << std::setw(8) << h5 << std::setw(8) << h6 << std::setw(8) << h7;
  return out.str();
}
}  // namespace sha256_impl

struct ParsedUrl {
  std::string host;
  int port = 80;
};

static bool parse_server_url(const std::string& url, ParsedUrl& parsed) {
  const std::string prefix = "http://";
  if (url.rfind(prefix, 0) != 0) return false;
  std::string hostport = url.substr(prefix.size());
  const auto slash = hostport.find('/');
  if (slash != std::string::npos) hostport = hostport.substr(0, slash);
  const auto colon = hostport.find(':');
  if (colon == std::string::npos) {
    parsed.host = hostport;
    parsed.port = 80;
  } else {
    parsed.host = hostport.substr(0, colon);
    parsed.port = std::atoi(hostport.substr(colon + 1).c_str());
  }
  return !parsed.host.empty();
}

static std::string url_encode_simple(const std::string& input) {
  std::ostringstream out;
  static const char* hex = "0123456789ABCDEF";
  for (unsigned char c : input) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out << static_cast<char>(c);
    } else {
      out << '%' << hex[(c >> 4) & 0x0f] << hex[c & 0x0f];
    }
  }
  return out.str();
}

static bool headersHaveChunked(std::string_view hdr) {
  size_t pos = 0;
  while (pos < hdr.size()) {
    const size_t eol = hdr.find("\r\n", pos);
    if (eol == std::string::npos) return false;
    const size_t lineLen = eol - pos;
    if (lineLen >= 18 && strncasecmp(hdr.data() + pos, "transfer-encoding:", 18) == 0) {
      size_t q = pos + 18;
      while (q < eol && (hdr[q] == ' ' || hdr[q] == '\t')) q++;
      for (; q + 7 <= eol; q++) {
        if (strncasecmp(hdr.data() + q, "chunked", 7) == 0) return true;
      }
    }
    pos = eol + 2;
  }
  return false;
}

static long contentLengthFromHeaders(std::string_view hdr) {
  size_t pos = 0;
  while (pos < hdr.size()) {
    const size_t eol = hdr.find("\r\n", pos);
    if (eol == std::string::npos) return -1;
    const size_t lineLen = eol - pos;
    if (lineLen >= 15 && strncasecmp(hdr.data() + pos, "content-length:", 15) == 0) {
      size_t v0 = pos + 15;
      while (v0 < eol && (hdr[v0] == ' ' || hdr[v0] == '\t')) v0++;
      return std::strtol(std::string(hdr.substr(v0, eol - v0)).c_str(), nullptr, 10);
    }
    pos = eol + 2;
  }
  return -1;
}

static bool decodeChunked(const std::vector<unsigned char>& in, std::vector<unsigned char>& out) {
  out.clear();
  size_t pos = 0;
  while (pos < in.size()) {
    const size_t line0 = pos;
    while (pos + 1 < in.size() && !(in[pos] == '\r' && in[pos + 1] == '\n')) pos++;
    if (pos + 1 >= in.size()) return false;
    std::string line(reinterpret_cast<const char*>(in.data() + line0), pos - line0);
    char* endhex = nullptr;
    unsigned long csz = std::strtoul(line.c_str(), &endhex, 16);
    if (endhex == line.c_str()) return false;
    pos += 2;
    if (csz == 0) break;
    if (csz > 100000000UL || pos + csz > in.size()) return false;
    out.insert(out.end(), in.begin() + static_cast<std::ptrdiff_t>(pos),
               in.begin() + static_cast<std::ptrdiff_t>(pos + csz));
    pos += csz;
    if (pos + 1 >= in.size() || in[pos] != '\r' || in[pos + 1] != '\n') return false;
    pos += 2;
  }
  return true;
}

static bool jsonWs(unsigned char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool jsonBoolAfterColon(std::string_view body, size_t& j, std::string_view word) {
  while (j < body.size() && jsonWs(static_cast<unsigned char>(body[j]))) j++;
  if (j + word.size() > body.size() || body.substr(j, word.size()) != word) return false;
  j += word.size();
  if (j < body.size()) {
    const unsigned char t = static_cast<unsigned char>(body[j]);
    if (!jsonWs(t) && t != ',' && t != '}' && t != ']') return false;
  }
  return true;
}

static bool jsonBodyHasAppliedMember(std::string_view body) {
  static constexpr std::string_view kApplied = "\"applied\"";
  for (size_t i = 0; i + kApplied.size() <= body.size(); i++) {
    if (body.substr(i, kApplied.size()) != kApplied) continue;
    const size_t after = i + kApplied.size();
    if (after < body.size()) {
      const unsigned char c = static_cast<unsigned char>(body[after]);
      if (std::isalnum(c) != 0 || c == '_') continue;
    }
    size_t j = after;
    while (j < body.size() && jsonWs(static_cast<unsigned char>(body[j]))) j++;
    if (j < body.size() && body[j] == ':') return true;
  }
  return false;
}

static bool jsonBodyAppliedIsTrue(std::string_view body) {
  static constexpr std::string_view kApplied = "\"applied\"";
  for (size_t i = 0; i + kApplied.size() <= body.size(); i++) {
    if (body.substr(i, kApplied.size()) != kApplied) continue;
    const size_t after = i + kApplied.size();
    if (after < body.size()) {
      const unsigned char c = static_cast<unsigned char>(body[after]);
      if (std::isalnum(c) != 0 || c == '_') continue;
    }
    size_t j = after;
    while (j < body.size() && jsonWs(static_cast<unsigned char>(body[j]))) j++;
    if (j >= body.size() || body[j] != ':') continue;
    j++;
    if (jsonBoolAfterColon(body, j, "true")) return true;
  }
  return false;
}

static bool jsonBodyAppliedIsFalse(std::string_view body) {
  static constexpr std::string_view kApplied = "\"applied\"";
  for (size_t i = 0; i + kApplied.size() <= body.size(); i++) {
    if (body.substr(i, kApplied.size()) != kApplied) continue;
    const size_t after = i + kApplied.size();
    if (after < body.size()) {
      const unsigned char c = static_cast<unsigned char>(body[after]);
      if (std::isalnum(c) != 0 || c == '_') continue;
    }
    size_t j = after;
    while (j < body.size() && jsonWs(static_cast<unsigned char>(body[j]))) j++;
    if (j >= body.size() || body[j] != ':') continue;
    j++;
    if (jsonBoolAfterColon(body, j, "false")) return true;
  }
  return false;
}

static bool http_request(
    const Config& cfg,
    const std::string& method,
    const std::string& target_path,
    const std::vector<unsigned char>& body,
    int& out_status,
    std::vector<unsigned char>& out_body,
    const char* content_type = nullptr) {
  ParsedUrl parsed;
  if (!parse_server_url(cfg.server_url, parsed)) return false;

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* res = nullptr;
  const std::string port = std::to_string(parsed.port);
  if (getaddrinfo(parsed.host.c_str(), port.c_str(), &hints, &res) != 0) return false;

  int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (sock < 0) {
    freeaddrinfo(res);
    return false;
  }
  if (connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
    close(sock);
    freeaddrinfo(res);
    return false;
  }
  freeaddrinfo(res);

  const char* ct = content_type ? content_type : "application/octet-stream";
  std::ostringstream request;
  request << method << " " << target_path << " HTTP/1.1\r\n";
  request << "Host: " << parsed.host << "\r\n";
  request << "Accept-Encoding: identity\r\n";
  request << "X-API-Key: " << cfg.api_key << "\r\n";
  request << "Content-Type: " << ct << "\r\n";
  request << "Content-Length: " << body.size() << "\r\n";
  request << "Connection: close\r\n\r\n";
  const std::string header = request.str();

  if (send(sock, header.data(), header.size(), 0) < 0) {
    close(sock);
    return false;
  }
  if (!body.empty() && send(sock, body.data(), body.size(), 0) < 0) {
    close(sock);
    return false;
  }

  std::vector<unsigned char> response;
  unsigned char buffer[1024];
  while (true) {
    const ssize_t n = recv(sock, buffer, sizeof(buffer), 0);
    if (n <= 0) break;
    response.insert(response.end(), buffer, buffer + n);
  }
  close(sock);
  if (response.empty()) return false;

  const std::string response_str(response.begin(), response.end());
  const auto status_end = response_str.find("\r\n");
  if (status_end == std::string::npos) return false;
  std::istringstream status_line(response_str.substr(0, status_end));
  std::string http_ver;
  status_line >> http_ver >> out_status;

  const auto body_pos = response_str.find("\r\n\r\n");
  if (body_pos == std::string::npos) return false;
  const std::string_view header_view(response_str.data(), body_pos);
  const size_t raw_begin = body_pos + 4;
  std::vector<unsigned char> raw_body(response.begin() + static_cast<std::ptrdiff_t>(raw_begin),
                                         response.end());
  if (headersHaveChunked(header_view)) {
    if (!decodeChunked(raw_body, out_body)) return false;
  } else {
    long cl = contentLengthFromHeaders(header_view);
    size_t use = raw_body.size();
    if (cl >= 0) {
      const auto cl_u = static_cast<size_t>(cl);
      if (cl_u < use) use = cl_u;
    }
    out_body.assign(raw_body.begin(), raw_body.begin() + static_cast<std::ptrdiff_t>(use));
  }
  return true;
}

static std::map<std::string, SaveMeta> parse_saves_json(const std::string& json) {
  std::map<std::string, SaveMeta> out;
  size_t pos = 0;
  while (true) {
    const auto gid = json.find("\"game_id\":\"", pos);
    if (gid == std::string::npos) break;
    const auto gstart = gid + 11;
    const auto gend = json.find('"', gstart);
    if (gend == std::string::npos) break;
    SaveMeta meta{};
    meta.game_id = json.substr(gstart, gend - gstart);

    const auto ts = json.find("\"last_modified_utc\":\"", gend);
    if (ts != std::string::npos) {
      const auto tstart = ts + 21;
      const auto tend = json.find('"', tstart);
      if (tend != std::string::npos) meta.last_modified_utc = json.substr(tstart, tend - tstart);
    }
    const auto su = json.find("\"server_updated_at\":\"", gend);
    if (su != std::string::npos) {
      const auto sstart = su + 21;
      const auto send = json.find('"', sstart);
      if (send != std::string::npos) meta.server_updated_at = json.substr(sstart, send - sstart);
    }
    const auto sh = json.find("\"sha256\":\"", gend);
    if (sh != std::string::npos) {
      const auto sstart = sh + 10;
      const auto send = json.find('"', sstart);
      if (send != std::string::npos) meta.sha256 = json.substr(sstart, send - sstart);
    }
    const auto fh = json.find("\"filename_hint\":\"", gend);
    if (fh != std::string::npos) {
      const auto fstart = fh + 17;
      const auto fend = json.find('"', fstart);
      if (fend != std::string::npos) meta.filename_hint = json.substr(fstart, fend - fstart);
    }
    out[meta.game_id] = meta;
    pos = gend + 1;
  }
  return out;
}

static std::string mtime_to_utc_iso(time_t mtime) {
  std::tm tm_utc{};
  gmtime_r(&mtime, &tm_utc);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S+00:00", &tm_utc);
  return std::string(buf);
}

static std::string id_map_file_path(const std::string& dir) {
  std::string d = dir;
  while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
  return d + "/.gbasync-idmap";
}

static std::vector<IdMapRow> id_map_load(const std::string& dir) {
  std::vector<IdMapRow> rows;
  std::ifstream in(id_map_file_path(dir));
  std::string line;
  while (rows.size() < 1024 && std::getline(in, line)) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    std::string stem = line.substr(0, tab);
    std::string gid = line.substr(tab + 1);
    while (!gid.empty() && (gid.back() == '\r' || gid.back() == '\n')) gid.pop_back();
    if (stem.empty() || gid.empty()) continue;
    rows.push_back({std::move(stem), std::move(gid)});
  }
  return rows;
}

static bool id_map_save(const std::string& dir, const std::vector<IdMapRow>& rows) {
  const std::string path = id_map_file_path(dir);
  const std::string tmp = path + ".tmp";
  std::ofstream out(tmp);
  if (!out) return false;
  for (const auto& r : rows) out << r.save_stem << '\t' << r.game_id << '\n';
  out.close();
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

static std::string id_map_lookup(const std::vector<IdMapRow>& rows, const std::string& stem) {
  for (const auto& r : rows) {
    if (r.save_stem == stem) return r.game_id;
  }
  return "";
}

static bool id_map_upsert(std::vector<IdMapRow>& rows, const std::string& stem, const std::string& gid) {
  for (auto& r : rows) {
    if (r.save_stem == stem) {
      if (r.game_id == gid) return false;
      r.game_id = gid;
      return true;
    }
  }
  rows.push_back({stem, gid});
  return true;
}

static std::vector<LocalSave> scan_local_saves(const Config& cfg, const std::string& dir) {
  std::vector<LocalSave> out;
  std::map<std::string, int> used_ids;
  std::vector<IdMapRow> id_map = id_map_load(dir);
  bool id_map_changed = false;
  std::vector<std::string> names;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  dirent* ent = nullptr;
  while ((ent = readdir(d)) != nullptr) {
    std::string name(ent->d_name);
    if (name == "." || name == ".." || !has_sav_extension(name)) continue;
    names.push_back(name);
  }
  closedir(d);
  std::sort(names.begin(), names.end());

  for (const auto& name : names) {
    LocalSave s{};
    s.name = name;
    s.path = dir + "/" + name;
    const std::string stem = file_stem(name);
    const std::string mapped_id = id_map_lookup(id_map, stem);
    const std::string resolved_id = resolve_game_id_for_save(cfg, name);
    const std::string fallback_id = sanitize_game_id(stem);
    std::string chosen_id = mapped_id.empty() ? (resolved_id.empty() ? fallback_id : resolved_id) : mapped_id;
    if (used_ids.find(chosen_id) != used_ids.end()) {
      // Ensure unique IDs per local save even when ROM headers collide.
      std::string base = fallback_id.empty() ? chosen_id : fallback_id;
      if (base.empty()) base = "unknown-game";
      chosen_id = base;
      int suffix = 2;
      while (used_ids.find(chosen_id) != used_ids.end()) {
        chosen_id = base + "-" + std::to_string(suffix++);
      }
    }
    id_map_changed = id_map_upsert(id_map, stem, chosen_id) || id_map_changed;
    used_ids[chosen_id] = 1;
    s.game_id = chosen_id;
    struct stat st{};
    if (stat(s.path.c_str(), &st) != 0) continue;
    s.st_mtime_unix = static_cast<std::int64_t>(st.st_mtime);
    if (st.st_mtime == 0) {
      // Some SD/FAT stacks leave mtime at epoch = "unset"; file can still be new.
      s.last_modified_utc = mtime_to_utc_iso(std::time(nullptr));
      s.mtime_trusted = true;
    } else {
      s.last_modified_utc = mtime_to_utc_iso(st.st_mtime);
      s.mtime_trusted = (st.st_mtime >= 946684800L);  // ignore pre-2000 mtimes (common bogus FAT values)
    }
    std::vector<unsigned char> bytes = read_file(s.path);
    s.size_bytes = bytes.size();
    s.sha256 = sha256_impl::hash(bytes);
    out.push_back(s);
  }
  if (id_map_changed) (void)id_map_save(dir, id_map);
  return out;
}

static std::string baseline_file_path(const Config& cfg) {
  std::string d = cfg.save_dir;
  while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
  return d + "/.gbasync-baseline";
}

static std::string baseline_legacy_file_path(const Config& cfg) {
  std::string d = cfg.save_dir;
  while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
  return d + "/.savesync-baseline";
}

static std::vector<BaselineRow> baseline_load(const Config& cfg) {
  std::vector<BaselineRow> rows;
  std::ifstream in(baseline_file_path(cfg));
  if (!in) in.open(baseline_legacy_file_path(cfg));
  std::string line;
  while (rows.size() < 256 && std::getline(in, line)) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    std::string gid = line.substr(0, tab);
    std::string sha = line.substr(tab + 1);
    while (!sha.empty() && (sha.back() == '\r' || sha.back() == '\n')) sha.pop_back();
    if (gid.empty() || sha.size() != 64) continue;
    rows.push_back({std::move(gid), std::move(sha)});
  }
  return rows;
}

static bool baseline_save(const Config& cfg, const std::vector<BaselineRow>& rows) {
  const std::string path = baseline_file_path(cfg);
  const std::string tmp = path + ".tmp";
  std::ofstream out(tmp);
  if (!out) return false;
  for (const auto& r : rows) out << r.game_id << '\t' << r.sha256 << '\n';
  out.close();
  std::remove(path.c_str());
  if (std::rename(tmp.c_str(), path.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  // Keep the legacy baseline file in sync so older client builds continue to work.
  const std::string legacy_path = baseline_legacy_file_path(cfg);
  const std::string legacy_tmp = legacy_path + ".tmp";
  std::ofstream legacy_out(legacy_tmp);
  if (legacy_out) {
    for (const auto& r : rows) legacy_out << r.game_id << '\t' << r.sha256 << '\n';
    legacy_out.close();
    std::remove(legacy_path.c_str());
    if (std::rename(legacy_tmp.c_str(), legacy_path.c_str()) != 0) std::remove(legacy_tmp.c_str());
  }
  return true;
}

static bool baseline_get_sha(const std::vector<BaselineRow>& rows, const std::string& id, std::string* out_sha) {
  for (const auto& r : rows) {
    if (r.game_id == id && r.sha256.size() == 64) {
      *out_sha = r.sha256;
      return true;
    }
  }
  return false;
}

static void baseline_upsert(std::vector<BaselineRow>& rows, const std::string& id, const std::string& sha) {
  for (auto& r : rows) {
    if (r.game_id == id) {
      r.sha256 = sha;
      return;
    }
  }
  rows.push_back({id, sha});
}

static std::string gbasync_status_path(const Config& cfg) {
  std::string d = cfg.save_dir;
  while (!d.empty() && (d.back() == '/' || d.back() == '\\')) d.pop_back();
  return d + "/.gbasync-status";
}

struct SyncStatusSnap {
  std::time_t last_unix = 0;
  bool last_ok = false;
  bool server_ok = false;
  int dropbox = -1;
  std::string err;
};

static bool sync_status_load(const Config& cfg, SyncStatusSnap* out) {
  std::ifstream in(gbasync_status_path(cfg));
  if (!in) return false;
  std::string line;
  while (std::getline(in, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = trim(line.substr(0, eq));
    std::string v = trim(line.substr(eq + 1));
    if (k == "t") out->last_unix = static_cast<std::time_t>(std::strtoll(v.c_str(), nullptr, 10));
    if (k == "ok") out->last_ok = (v == "1");
    if (k == "srv") out->server_ok = (v == "1");
    if (k == "db") {
      if (v == "u") out->dropbox = -1;
      else if (v == "0") out->dropbox = 0;
      else if (v == "1") out->dropbox = 1;
    }
    if (k == "e") out->err = v;
  }
  return true;
}

static bool sync_status_save(const Config& cfg, const SyncStatusSnap& s) {
  std::ostringstream o;
  o << "v=1\n";
  o << "t=" << static_cast<long long>(s.last_unix) << "\n";
  o << "ok=" << (s.last_ok ? "1" : "0") << "\n";
  o << "srv=" << (s.server_ok ? "1" : "0") << "\n";
  if (s.dropbox < 0) o << "db=u\n";
  else o << "db=" << s.dropbox << "\n";
  o << "e=" << s.err << "\n";
  const std::string str = o.str();
  std::vector<unsigned char> data(str.begin(), str.end());
  return write_atomic(gbasync_status_path(cfg), data);
}

static void sync_status_after_server_work(const Config& cfg, bool last_ok, bool server_ok, const char* err_short) {
  SyncStatusSnap s{};
  (void)sync_status_load(cfg, &s);
  s.last_unix = std::time(nullptr);
  s.last_ok = last_ok;
  s.server_ok = server_ok;
  s.err = err_short ? err_short : "";
  (void)sync_status_save(cfg, s);
}

static void sync_status_after_dropbox_only(const Config& cfg, bool http_ok) {
  SyncStatusSnap s{};
  (void)sync_status_load(cfg, &s);
  s.last_unix = std::time(nullptr);
  s.last_ok = http_ok;
  s.server_ok = http_ok;
  s.dropbox = http_ok ? 1 : 0;
  s.err.clear();
  (void)sync_status_save(cfg, s);
}

static void sync_status_print_menu(const Config& cfg) {
  SyncStatusSnap s{};
  if (!sync_status_load(cfg, &s)) {
    printf("Last sync: (none)\n");
    printf("Server: —\n");
    printf("Dropbox: —\n\n");
    return;
  }
  char tbuf[32] = "unknown";
  if (s.last_unix > 0) {
    std::tm tm_utc{};
    gmtime_r(&s.last_unix, &tm_utc);
    std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M UTC", &tm_utc);
  }
  const char* srv = s.server_ok ? "OK" : "fail";
  const char* db = (s.dropbox < 0) ? "—" : (s.dropbox ? "OK" : "fail");
  const char* ok = s.last_ok ? "OK" : "fail";
  printf("Last sync: %s %s\n", ok, tbuf);
  printf("Server: %s\n", srv);
  printf("Dropbox: %s\n", db);
  if (!s.err.empty()) printf("Last error: %.60s\n", s.err.c_str());
  printf("\n");
}

enum class AutoPlanKind {
  Locked,
  Ok,
  Upload,
  Download,
  SkipNoBaseline,
  Conflict,
};

struct AutoPlanRow {
  std::string id;
  AutoPlanKind kind = AutoPlanKind::Ok;
};

static AutoPlanKind classify_auto_row(
    const std::string& id,
    bool has_l,
    bool has_r,
    const LocalSave* l,
    const SaveMeta* r,
    const std::vector<BaselineRow>& baseline,
    bool locked) {
  if (has_l && has_r) {
    if (l->sha256 == r->sha256) return AutoPlanKind::Ok;
    if (locked) return AutoPlanKind::Locked;
    std::string base_sha;
    if (!baseline_get_sha(baseline, id, &base_sha)) return AutoPlanKind::SkipNoBaseline;
    const bool loc_eq = (strcasecmp(l->sha256.c_str(), base_sha.c_str()) == 0);
    const bool rem_eq = (strcasecmp(r->sha256.c_str(), base_sha.c_str()) == 0);
    if (loc_eq && !rem_eq) return AutoPlanKind::Download;
    if (!loc_eq && rem_eq) return AutoPlanKind::Upload;
    if (!loc_eq && !rem_eq) return AutoPlanKind::Conflict;
  } else if (has_l && !has_r) {
    return AutoPlanKind::Upload;
  } else if (!has_l && has_r) {
    return AutoPlanKind::Download;
  }
  return AutoPlanKind::Ok;
}

static bool parse_ini_key_value(const std::string& line, std::string* key, std::string* val) {
  const auto eq = line.find('=');
  if (eq == std::string::npos) return false;
  *key = trim(line.substr(0, eq));
  *val = trim(line.substr(eq + 1));
  return !key->empty();
}

static bool save_locked_ids_to_ini(const std::string& path, const std::set<std::string>& locked) {
  std::vector<std::string> lines;
  {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
  }
  std::ostringstream joined;
  for (auto it = locked.begin(); it != locked.end(); ++it) {
    if (it != locked.begin()) joined << ",";
    joined << *it;
  }
  const std::string new_val = joined.str();

  bool in_sync = false;
  bool replaced = false;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string t = trim(lines[i]);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
      in_sync = (t == "[sync]");
      continue;
    }
    if (in_sync) {
      std::string k;
      std::string v;
      if (parse_ini_key_value(lines[i], &k, &v) && k == "locked_ids") {
        lines[i] = std::string("locked_ids=") + new_val;
        replaced = true;
        break;
      }
    }
  }
  if (!replaced) {
    for (size_t i = 0; i < lines.size(); ++i) {
      if (trim(lines[i]) == "[sync]") {
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i + 1), std::string("locked_ids=") + new_val);
        replaced = true;
        break;
      }
    }
  }
  if (!replaced) {
    if (!lines.empty()) lines.push_back("");
    lines.push_back("[sync]");
    lines.push_back(std::string("locked_ids=") + new_val);
  }
  std::ostringstream out;
  for (size_t i = 0; i < lines.size(); ++i) out << lines[i] << "\n";
  const std::string data = out.str();
  return write_atomic(path, std::vector<unsigned char>(data.begin(), data.end()));
}

static void build_auto_plan_vector(
    const Config& cfg,
    const std::vector<std::string>& merge_ids,
    const std::map<std::string, LocalSave>& local_by_id,
    const std::map<std::string, SaveMeta>& remote,
    const std::vector<BaselineRow>& baseline,
    std::vector<AutoPlanRow>& plan) {
  plan.clear();
  plan.reserve(merge_ids.size());
  for (const std::string& id : merge_ids) {
    auto lit = local_by_id.find(id);
    auto rit = remote.find(id);
    const bool has_l = lit != local_by_id.end();
    const bool has_r = rit != remote.end();
    const bool locked = cfg.locked_ids.count(to_lower(id)) > 0;
    AutoPlanKind k = AutoPlanKind::Ok;
    if (has_l && has_r) {
      k = classify_auto_row(id, true, true, &lit->second, &rit->second, baseline, locked);
    } else if (has_l && !has_r) {
      k = locked ? AutoPlanKind::Locked : AutoPlanKind::Upload;
    } else if (!has_l && has_r) {
      k = locked ? AutoPlanKind::Locked : AutoPlanKind::Download;
    }
    plan.push_back({id, k});
  }
}

static void recount_auto_plan(const std::vector<AutoPlanRow>& plan, int* nu, int* nd, int* ns, int* nc, int* nl, int* nk) {
  int u = 0, d = 0, s = 0, c = 0, l = 0, k = 0;
  for (const auto& row : plan) {
    switch (row.kind) {
      case AutoPlanKind::Upload:
        u++;
        break;
      case AutoPlanKind::Download:
        d++;
        break;
      case AutoPlanKind::SkipNoBaseline:
        s++;
        break;
      case AutoPlanKind::Conflict:
        c++;
        break;
      case AutoPlanKind::Locked:
        l++;
        break;
      case AutoPlanKind::Ok:
        k++;
        break;
    }
  }
  if (nu) *nu = u;
  if (nd) *nd = d;
  if (ns) *ns = s;
  if (nc) *nc = c;
  if (nl) *nl = l;
  if (nk) *nk = k;
}

static const char* auto_plan_kind_label(AutoPlanKind k) {
  switch (k) {
    case AutoPlanKind::Locked:
      return "SKIP (locked)";
    case AutoPlanKind::Ok:
      return "OK";
    case AutoPlanKind::Upload:
      return "UPLOAD";
    case AutoPlanKind::Download:
      return "DOWNLOAD";
    case AutoPlanKind::SkipNoBaseline:
      return "SKIP (no baseline)";
    case AutoPlanKind::Conflict:
      return "CONFLICT (prompt)";
    default:
      return "?";
  }
}

static constexpr const char kSwitchConfigIni[] = "sdmc:/switch/gba-sync/config.ini";

static bool preview_auto_plan(
    PadState* pad,
    Config& cfg,
    const std::vector<std::string>& merge_ids,
    const std::map<std::string, LocalSave>& local_by_id,
    const std::map<std::string, SaveMeta>& remote,
    const std::vector<BaselineRow>& baseline,
    std::vector<AutoPlanRow>& plan) {
  int nu = 0, nd = 0, ns = 0, nc = 0, nl = 0, nk = 0;
  build_auto_plan_vector(cfg, merge_ids, local_by_id, remote, baseline, plan);
  recount_auto_plan(plan, &nu, &nd, &ns, &nc, &nl, &nk);

  std::vector<size_t> filt;
  filt.reserve(plan.size());
  for (size_t i = 0; i < plan.size(); i++) {
    if (plan[i].kind != AutoPlanKind::Ok) filt.push_back(i);
  }
  int cursor = 0;
  constexpr int kVisible = 14;
  int scroll = 0;
  const int n_filt = static_cast<int>(filt.size());
  int total_rows = std::max(1, n_filt);
  bool dirty = true;

  constexpr u64 kSyncMask =
      HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Plus;
  while (appletMainLoop()) {
    padUpdate(pad);
    (void)padGetButtonsDown(pad);
    if ((padGetButtons(pad) & kSyncMask) == 0) break;
    consoleUpdate(NULL);
  }

  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_A) return true;
    if (down & HidNpadButton_B) return false;
    if (n_filt > 0) {
      if (down & HidNpadButton_Up) {
        cursor = (cursor + total_rows - 1) % total_rows;
        dirty = true;
      }
      if (down & HidNpadButton_Down) {
        cursor = (cursor + 1) % total_rows;
        dirty = true;
      }
      if (cursor < scroll) scroll = cursor;
      if (cursor >= scroll + kVisible) scroll = cursor - kVisible + 1;
      if (scroll < 0) scroll = 0;
      const int max_scroll = std::max(0, total_rows - kVisible);
      if (scroll > max_scroll) scroll = max_scroll;
    }

    if (!dirty) {
      consoleUpdate(NULL);
      continue;
    }

    consoleClear();
    printf("--- Sync preview ---\n");
    printf("UP:%d DOWN:%d SKIP:%d CONF:%d LOCK:%d\n", nu, nd, ns, nc, nl);
    printf("\n");
    {
      constexpr int kPrevCol = 20;
      printf("%-*s%s\n", kPrevCol, "A: confirm", "");
      printf("%-*s%s\n", kPrevCol, "B: back", "");
    }
    printf("\n");
    for (int row = scroll; row < std::min(scroll + kVisible, n_filt); row++) {
      const char mark = (row == cursor) ? '>' : ' ';
      const AutoPlanRow& pr = plan[filt[static_cast<size_t>(row)]];
      printf("%c %.28s  %s\n", mark, pr.id.c_str(), auto_plan_kind_label(pr.kind));
    }
    dirty = false;
    consoleUpdate(NULL);
  }
  return false;
}

static void debug_report_sync_start_switch(const Config& cfg, const std::vector<LocalSave>& local) {
  int untrusted = 0;
  for (const auto& s : local) {
    if (!s.mtime_trusted) untrusted++;
  }
  std::ostringstream json;
  json << "{\"utc_iso\":\"" << mtime_to_utc_iso(std::time(nullptr))
       << "\",\"platform\":\"switch-homebrew\",\"phase\":\"sync_auto_start\",\"untrusted_local_saves\":"
       << untrusted << "}";
  const std::string jstr = json.str();
  std::vector<unsigned char> post_body(jstr.begin(), jstr.end());
  int st = 0;
  std::vector<unsigned char> resp;
  (void)http_request(cfg, "POST", "/debug/client-clock", post_body, st, resp, "application/json");
}

static bool bodyContainsSha256Value(const std::string& body, const std::string& expect) {
  if (expect.size() != 64) return false;
  const std::string key = "\"sha256\"";
  size_t pos = 0;
  while ((pos = body.find(key, pos)) != std::string::npos) {
    size_t j = pos + key.size();
    while (j < body.size() && std::isspace(static_cast<unsigned char>(body[j]))) j++;
    if (j >= body.size() || body[j] != ':') {
      pos++;
      continue;
    }
    j++;
    while (j < body.size() && (body[j] == ' ' || body[j] == '\t')) j++;
    if (j >= body.size() || body[j] != '"') {
      pos++;
      continue;
    }
    j++;
    if (j + 64 > body.size()) {
      pos++;
      continue;
    }
    std::string candidate = body.substr(j, 64);
    bool hex = true;
    for (char c : candidate) {
      if (!std::isxdigit(static_cast<unsigned char>(c))) {
        hex = false;
        break;
      }
    }
    if (hex && strcasecmp(candidate.c_str(), expect.c_str()) == 0) return true;
    pos = j;
  }
  return false;
}

static bool put_save_log(const Config& cfg,
                         const LocalSave& l,
                         bool force,
                         const std::string& platform,
                         std::vector<std::string>& logs) {
  const std::vector<unsigned char> bytes = read_file(l.path);
  std::ostringstream path;
  path << "/save/" << l.game_id << "?last_modified_utc=" << url_encode_simple(l.last_modified_utc)
       << "&sha256=" << url_encode_simple(l.sha256) << "&size_bytes=" << l.size_bytes
       << "&filename_hint=" << url_encode_simple(l.name) << "&platform_source=" << platform
       << "&client_clock_utc=" << url_encode_simple(mtime_to_utc_iso(std::time(nullptr)));
  if (force) path << "&force=1";
  std::vector<unsigned char> put_resp;
  int put_status = 0;
  if (!http_request(cfg, "PUT", path.str(), bytes, put_status, put_resp) || put_status != 200) {
    logs.push_back(l.game_id + ": ERROR(upload)");
    return false;
  }
  const std::string body(put_resp.begin(), put_resp.end());
  const std::string_view bv(body);
  if (jsonBodyAppliedIsFalse(bv)) {
    logs.push_back(l.game_id + ": REJECTED (server kept existing copy)");
    return false;
  }
  if (jsonBodyAppliedIsTrue(bv) || !jsonBodyHasAppliedMember(bv)) {
    logs.push_back(l.game_id + ": UPLOADED");
    return true;
  }
  bool ok = bodyContainsSha256Value(body, l.sha256);
  if (!ok) {
    std::vector<unsigned char> meta_body;
    int mst = 0;
    if (http_request(cfg, "GET", "/save/" + l.game_id + "/meta", {}, mst, meta_body) && mst == 200) {
      const std::string mb(meta_body.begin(), meta_body.end());
      ok = bodyContainsSha256Value(mb, l.sha256);
    }
  }
  if (!ok) {
    logs.push_back(l.game_id + ": REJECTED (could not confirm save on server)");
    return false;
  }
  logs.push_back(l.game_id + ": UPLOADED");
  return true;
}

static bool get_save_log(const Config& cfg, const std::string& id, const SaveMeta& r, std::vector<std::string>& logs) {
  std::string fallback_name = id + ".sav";
  std::string target_name = r.filename_hint.empty() ? fallback_name : sanitize_filename(r.filename_hint, fallback_name);
  if (!has_sav_extension(target_name)) target_name += ".sav";
  const std::string target_path = cfg.save_dir + "/" + target_name;
  std::vector<unsigned char> save_data;
  int get_status = 0;
  if (!http_request(cfg, "GET", "/save/" + id, {}, get_status, save_data) || get_status != 200) {
    logs.push_back(id + ": ERROR(download)");
    return false;
  }
  if (write_atomic(target_path, save_data)) {
    logs.push_back(id + ": DOWNLOADED");
    return true;
  }
  logs.push_back(id + ": ERROR(write)");
  return false;
}

static void resolve_both_changed_conflict_switch(PadState* pad,
                                                 const Config& cfg,
                                                 const std::string& id,
                                                 const LocalSave& l,
                                                 const SaveMeta& r,
                                                 const std::string& plat,
                                                 std::vector<std::string>& logs,
                                                 std::vector<BaselineRow>& baseline) {
  consoleClear();
  printf("\n  -------- Conflict --------\n\n  %s\n\n", id.c_str());
  printf("  Local and server both changed since\n");
  printf("  the last successful sync.\n\n");
  printf("  X   Upload local (overwrite server)\n");
  printf("  Y   Download server (overwrite local)\n");
  printf("  B   Skip for now\n\n");
  consoleUpdate(NULL);
  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_X) {
      if (put_save_log(cfg, l, true, plat, logs)) baseline_upsert(baseline, id, l.sha256);
      break;
    }
    if (down & HidNpadButton_Y) {
      if (get_save_log(cfg, id, r, logs)) baseline_upsert(baseline, id, r.sha256);
      break;
    }
    if (down & HidNpadButton_B) break;
    consoleUpdate(NULL);
  }
}

static bool pick_upload_selection(PadState* pad, const Config& cfg, SyncManualFilter* out) {
  auto locals = scan_local_saves(cfg, cfg.save_dir);
  if (locals.empty()) {
    printf("No local .sav files to upload.\n");
    return false;
  }
  const int n = static_cast<int>(locals.size());
  std::vector<bool> picked(static_cast<size_t>(n), true);
  bool master_all = true;
  int cursor = 0;
  int scroll = 0;
  constexpr int kVisible = 16;
  bool dirty = true;

  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_Plus) {
      if (master_all) {
        out->all = true;
        out->ids.clear();
        return true;
      }
      out->all = false;
      out->ids.clear();
      for (int i = 0; i < n; i++) {
        if (picked[static_cast<size_t>(i)]) out->ids.insert(locals[static_cast<size_t>(i)].game_id);
      }
      if (out->ids.empty()) continue;
      return true;
    }
    if (down & HidNpadButton_B) return false;

    const int total_rows = n + 1;
    if (down & HidNpadButton_Up) {
      cursor = (cursor + total_rows - 1) % total_rows;
      dirty = true;
    }
    if (down & HidNpadButton_Down) {
      cursor = (cursor + 1) % total_rows;
      dirty = true;
    }
    if (down & HidNpadButton_A) {
      if (cursor == 0) {
        master_all = !master_all;
        const bool v = master_all;
        for (int i = 0; i < n; i++) picked[static_cast<size_t>(i)] = v;
      } else {
        picked[static_cast<size_t>(cursor - 1)] = !picked[static_cast<size_t>(cursor - 1)];
        master_all = true;
        for (int i = 0; i < n; i++) {
          if (!picked[static_cast<size_t>(i)]) master_all = false;
        }
      }
      dirty = true;
    }

    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + kVisible) scroll = cursor - kVisible + 1;
    if (scroll < 0) scroll = 0;
    const int max_scroll = std::max(0, total_rows - kVisible);
    if (scroll > max_scroll) scroll = max_scroll;

    if (!dirty) {
      consoleUpdate(NULL);
      continue;
    }

    consoleClear();
    printf("Upload: choose saves\n");
    printf("D-pad: move   A: toggle   +: run upload   B: back to menu\n\n");
    for (int row = scroll; row < std::min(scroll + kVisible, total_rows); row++) {
      const char mark = (row == cursor) ? '>' : ' ';
      if (row == 0) {
        printf("%c [%c] ALL SAVES\n", mark, master_all ? 'x' : ' ');
      } else {
        const LocalSave& L = locals[static_cast<size_t>(row - 1)];
        printf(
            "%c [%c] %.28s\n",
            mark,
            picked[static_cast<size_t>(row - 1)] ? 'x' : ' ',
            L.game_id.c_str());
      }
    }
    dirty = false;
    consoleUpdate(NULL);
  }
  return false;
}

static bool pick_download_selection(PadState* pad, const Config& cfg, SyncManualFilter* out) {
  int status = 0;
  std::vector<unsigned char> body;
  if (!http_request(cfg, "GET", "/saves", {}, status, body) || status != 200) {
    printf("ERROR: GET /saves failed (cannot list downloads)\n");
    return false;
  }
  std::string json(body.begin(), body.end());
  auto remote = parse_saves_json(json);
  if (remote.empty()) {
    printf("No remote saves to download.\n");
    return false;
  }
  std::vector<std::pair<std::string, SaveMeta>> rows;
  rows.reserve(remote.size());
  for (const auto& kv : remote) rows.push_back(kv);
  std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  const int n = static_cast<int>(rows.size());
  std::vector<bool> picked(static_cast<size_t>(n), true);
  bool master_all = true;
  int cursor = 0;
  int scroll = 0;
  constexpr int kVisible = 16;
  bool dirty = true;

  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_Plus) {
      if (master_all) {
        out->all = true;
        out->ids.clear();
        return true;
      }
      out->all = false;
      out->ids.clear();
      for (int i = 0; i < n; i++) {
        if (picked[static_cast<size_t>(i)]) out->ids.insert(rows[static_cast<size_t>(i)].first);
      }
      if (out->ids.empty()) continue;
      return true;
    }
    if (down & HidNpadButton_B) return false;

    const int total_rows = n + 1;
    if (down & HidNpadButton_Up) {
      cursor = (cursor + total_rows - 1) % total_rows;
      dirty = true;
    }
    if (down & HidNpadButton_Down) {
      cursor = (cursor + 1) % total_rows;
      dirty = true;
    }
    if (down & HidNpadButton_A) {
      if (cursor == 0) {
        master_all = !master_all;
        const bool v = master_all;
        for (int i = 0; i < n; i++) picked[static_cast<size_t>(i)] = v;
      } else {
        picked[static_cast<size_t>(cursor - 1)] = !picked[static_cast<size_t>(cursor - 1)];
        master_all = true;
        for (int i = 0; i < n; i++) {
          if (!picked[static_cast<size_t>(i)]) master_all = false;
        }
      }
      dirty = true;
    }

    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + kVisible) scroll = cursor - kVisible + 1;
    if (scroll < 0) scroll = 0;
    const int max_scroll = std::max(0, total_rows - kVisible);
    if (scroll > max_scroll) scroll = max_scroll;

    if (!dirty) {
      consoleUpdate(NULL);
      continue;
    }

    consoleClear();
    printf("Download: choose saves\n");
    printf("D-pad: move   A: toggle   +: run download   B: back to menu\n\n");
    for (int row = scroll; row < std::min(scroll + kVisible, total_rows); row++) {
      const char mark = (row == cursor) ? '>' : ' ';
      if (row == 0) {
        printf("%c [%c] ALL SAVES\n", mark, master_all ? 'x' : ' ');
      } else {
        const SaveMeta& R = rows[static_cast<size_t>(row - 1)].second;
        printf(
            "%c [%c] %.28s\n",
            mark,
            picked[static_cast<size_t>(row - 1)] ? 'x' : ' ',
            R.game_id.c_str());
      }
    }
    dirty = false;
    consoleUpdate(NULL);
  }
  return false;
}

static void save_viewer_switch(PadState* pad, Config& cfg) {
  auto locals = scan_local_saves(cfg, cfg.save_dir);
  std::map<std::string, LocalSave> local_by_id;
  for (const auto& s : locals) local_by_id[s.game_id] = s;
  int status = 0;
  std::vector<unsigned char> body;
  if (!http_request(cfg, "GET", "/saves", {}, status, body) || status != 200) {
    consoleClear();
    printf("Save viewer: GET /saves failed");
    if (status > 0) printf(" (HTTP %d)", status);
    printf("\nB: back\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
      padUpdate(pad);
      if (padGetButtonsDown(pad) & HidNpadButton_B) return;
      consoleUpdate(NULL);
    }
    return;
  }
  std::string json(body.begin(), body.end());
  auto remote = parse_saves_json(json);
  std::vector<std::string> merge_ids;
  merge_ids.reserve(local_by_id.size() + remote.size());
  for (const auto& [id, _] : local_by_id) merge_ids.push_back(id);
  for (const auto& [id, _] : remote) {
    if (!local_by_id.count(id)) merge_ids.push_back(id);
  }
  std::sort(merge_ids.begin(), merge_ids.end());
  if (merge_ids.empty()) {
    consoleClear();
    printf("Save viewer: no saves (local or server).\n");
    printf("B: back\n");
    consoleUpdate(NULL);
    while (appletMainLoop()) {
      padUpdate(pad);
      if (padGetButtonsDown(pad) & HidNpadButton_B) return;
      consoleUpdate(NULL);
    }
    return;
  }
  int cursor = 0;
  int scroll = 0;
  constexpr int kVisible = 16;
  const int total_rows = static_cast<int>(merge_ids.size());
  bool dirty = true;
  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_B) return;
    if (down & HidNpadButton_R) {
      const std::string& gid = merge_ids[static_cast<size_t>(cursor)];
      const std::string lk = to_lower(gid);
      if (cfg.locked_ids.count(lk)) {
        cfg.locked_ids.erase(lk);
      } else {
        cfg.locked_ids.insert(lk);
      }
      (void)save_locked_ids_to_ini(kSwitchConfigIni, cfg.locked_ids);
      dirty = true;
    }
    if (down & HidNpadButton_Up) {
      cursor = (cursor + total_rows - 1) % total_rows;
      dirty = true;
    }
    if (down & HidNpadButton_Down) {
      cursor = (cursor + 1) % total_rows;
      dirty = true;
    }
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + kVisible) scroll = cursor - kVisible + 1;
    if (scroll < 0) scroll = 0;
    const int max_scroll = std::max(0, total_rows - kVisible);
    if (scroll > max_scroll) scroll = max_scroll;

    if (!dirty) {
      consoleUpdate(NULL);
      continue;
    }

    consoleClear();
    printf("--- Save viewer (lock for Auto) ---\n");
    printf("\n");
    {
      constexpr int kMenuLeftCol = 22;
      printf("%-*s%s\n", kMenuLeftCol, "UP/DOWN: move", "");
      printf("%-*s%s\n", kMenuLeftCol, "R: toggle lock -> config", "");
      printf("%-*s%s\n", kMenuLeftCol, "B: back", "");
    }
    printf("\n");
    for (int row = scroll; row < std::min(scroll + kVisible, total_rows); row++) {
      const char mark = (row == cursor) ? '>' : ' ';
      const std::string& id = merge_ids[static_cast<size_t>(row)];
      const std::string lk = to_lower(id);
      const char* tag = cfg.locked_ids.count(lk) ? "[L]" : "   ";
      printf("%c%s %.28s\n", mark, tag, id.c_str());
    }
    dirty = false;
    consoleUpdate(NULL);
  }
}

static std::vector<std::string> run_sync(
    Config& cfg,
    SyncAction action,
    const SyncManualFilter* xy_filter,
    PadState* pad,
    bool* out_already_up_to_date) {
  if (out_already_up_to_date) *out_already_up_to_date = false;
  std::vector<std::string> logs;
  if (action == SyncAction::Auto) {
    printf("\n");
    printf("Scanning local saves...\n");
    consoleUpdate(NULL);
  }
  auto local = scan_local_saves(cfg, cfg.save_dir);
  std::map<std::string, LocalSave> local_by_id;
  for (const auto& s : local) local_by_id[s.game_id] = s;
  if (action != SyncAction::Auto) {
    logs.push_back("Local saves: " + std::to_string(local.size()));
  }

  std::vector<BaselineRow> baseline = baseline_load(cfg);

  int status = 0;
  std::vector<unsigned char> body;
  if (!http_request(cfg, "GET", "/saves", {}, status, body) || status != 200) {
    if (status == 401) {
      logs.push_back("ERROR: GET /saves unauthorized (check api_key)");
    } else if (status > 0) {
      logs.push_back("ERROR: GET /saves failed (HTTP " + std::to_string(status) + ")");
    } else {
      logs.push_back("ERROR: GET /saves failed (network/connect)");
    }
    sync_status_after_server_work(cfg, false, false, "GET /saves");
    return logs;
  }
  std::string json(body.begin(), body.end());
  auto remote = parse_saves_json(json);
  if (action != SyncAction::Auto) {
    logs.push_back("Remote saves: " + std::to_string(remote.size()));
  }

  const std::string plat = "switch-homebrew";

  if (action == SyncAction::UploadOnly) {
    for (const auto& entry : local_by_id) {
      if (xy_filter && !xy_filter->all && !xy_filter->ids.count(entry.first)) continue;
      if (put_save_log(cfg, entry.second, true, plat, logs))
        baseline_upsert(baseline, entry.first, entry.second.sha256);
    }
    if (!baseline_save(cfg, baseline)) logs.push_back("WARN: could not write .gbasync-baseline");
    sync_status_after_server_work(cfg, true, true, "");
    return logs;
  }

  if (action == SyncAction::DownloadOnly) {
    for (const auto& [id, r] : remote) {
      if (xy_filter && !xy_filter->all && !xy_filter->ids.count(id)) continue;
      if (get_save_log(cfg, id, r, logs)) baseline_upsert(baseline, id, r.sha256);
    }
    if (!baseline_save(cfg, baseline)) logs.push_back("WARN: could not write .gbasync-baseline");
    sync_status_after_server_work(cfg, true, true, "");
    return logs;
  }

  /* Auto: hash + .gbasync-baseline (legacy .savesync-baseline still read). */
  debug_report_sync_start_switch(cfg, local);
  std::vector<std::string> merge_ids;
  merge_ids.reserve(local_by_id.size() + remote.size());
  for (const auto& [id, _] : local_by_id) merge_ids.push_back(id);
  for (const auto& [id, _] : remote) {
    if (!local_by_id.count(id)) merge_ids.push_back(id);
  }

  std::vector<AutoPlanRow> plan;
  build_auto_plan_vector(cfg, merge_ids, local_by_id, remote, baseline, plan);
  int nu = 0, nd = 0, ns = 0, nc = 0, nl = 0, nk = 0;
  recount_auto_plan(plan, &nu, &nd, &ns, &nc, &nl, &nk);
  /* All rows OK (in sync). Not nu+nd+ns+nc==0 — that matches "all locked" too. */
  if (nk == static_cast<int>(plan.size())) {
    for (const std::string& id : merge_ids) {
      if (cfg.locked_ids.count(to_lower(id)) > 0) continue;
      auto lit = local_by_id.find(id);
      auto rit = remote.find(id);
      if (lit != local_by_id.end() && rit != remote.end() && lit->second.sha256 == rit->second.sha256) {
        baseline_upsert(baseline, id, lit->second.sha256);
      }
    }
    if (!baseline_save(cfg, baseline)) logs.push_back("WARN: could not write .gbasync-baseline");
    sync_status_after_server_work(cfg, true, true, "");
    logs.push_back("");
    logs.push_back("Already Up To Date");
    if (out_already_up_to_date) *out_already_up_to_date = true;
    return logs;
  }

  if (!pad || !preview_auto_plan(pad, cfg, merge_ids, local_by_id, remote, baseline, plan)) {
    logs.push_back("Preview cancelled.");
    return logs;
  }

  /* Blank line before per-game apply lines (matches 3DS printf("\n") after confirm). */
  logs.push_back("");

  for (const std::string& id : merge_ids) {
    auto lit = local_by_id.find(id);
    auto rit = remote.find(id);
    const bool has_l = lit != local_by_id.end();
    const bool has_r = rit != remote.end();
    if (cfg.locked_ids.count(to_lower(id)) > 0) {
      logs.push_back(id + ": SKIP (locked on this device)");
      continue;
    }

    if (has_l && has_r) {
      const LocalSave& l = lit->second;
      const SaveMeta& r = rit->second;
      if (l.sha256 == r.sha256) {
        baseline_upsert(baseline, id, l.sha256);
        continue;
      }
      std::string base_sha;
      if (!baseline_get_sha(baseline, id, &base_sha)) {
        logs.push_back(
            id +
            ": SKIP (no baseline yet — use X or Y once per game, then Auto works)");
        continue;
      }
      const bool loc_eq = (strcasecmp(l.sha256.c_str(), base_sha.c_str()) == 0);
      const bool rem_eq = (strcasecmp(r.sha256.c_str(), base_sha.c_str()) == 0);
      if (loc_eq && !rem_eq) {
        if (get_save_log(cfg, id, r, logs)) baseline_upsert(baseline, id, r.sha256);
      } else if (!loc_eq && rem_eq) {
        if (put_save_log(cfg, l, false, plat, logs)) baseline_upsert(baseline, id, l.sha256);
      } else if (!loc_eq && !rem_eq) {
        if (pad) {
          resolve_both_changed_conflict_switch(pad, cfg, id, l, r, plat, logs, baseline);
        } else {
          logs.push_back(id + ": SKIP (both changed — need interactive resolution)");
        }
      }
    } else if (has_l && !has_r) {
      if (put_save_log(cfg, lit->second, false, plat, logs))
        baseline_upsert(baseline, id, lit->second.sha256);
    } else if (!has_l && has_r) {
      if (get_save_log(cfg, id, rit->second, logs)) baseline_upsert(baseline, id, rit->second.sha256);
    }
  }

  if (!baseline_save(cfg, baseline)) logs.push_back("WARN: could not write .gbasync-baseline");
  sync_status_after_server_work(cfg, true, true, "");
  return logs;
}

static std::vector<std::string> run_dropbox_sync_once(const Config& cfg) {
  std::vector<std::string> logs;
  int status = 0;
  std::vector<unsigned char> body;
  std::vector<unsigned char> resp;
  if (!http_request(cfg, "POST", "/dropbox/sync-once", body, status, resp, "application/json")) {
    logs.push_back("Dropbox sync request: ERROR(request)");
    sync_status_after_dropbox_only(cfg, false);
    return logs;
  }
  if (status == 200) {
    logs.push_back("Dropbox sync request: OK");
    sync_status_after_dropbox_only(cfg, true);
  } else {
    logs.push_back("Dropbox sync request: HTTP " + std::to_string(status));
    sync_status_after_dropbox_only(cfg, false);
  }
  return logs;
}

static bool choose_action(PadState* pad, SyncAction* out_action) {
  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_A) {
      *out_action = SyncAction::Auto;
      return true;
    }
    if (down & HidNpadButton_X) {
      *out_action = SyncAction::UploadOnly;
      return true;
    }
    if (down & HidNpadButton_Y) {
      *out_action = SyncAction::DownloadOnly;
      return true;
    }
    if (down & HidNpadButton_Minus) {
      *out_action = SyncAction::DropboxSync;
      return true;
    }
    if (down & HidNpadButton_R) {
      *out_action = SyncAction::SaveViewer;
      return true;
    }
    if (down & HidNpadButton_Plus) {
      return false;
    }
    consoleUpdate(NULL);
  }
  return false;
}

static void wait_after_sync_switch(PadState* pad, bool* quit_app, bool can_reboot) {
  if (can_reboot) {
    printf("\nA: main menu   Y: reboot now   +: exit app\n");
  } else {
    printf("\nA: main menu   +: exit app\n");
  }
  while (appletMainLoop()) {
    padUpdate(pad);
    const u64 down = padGetButtonsDown(pad);
    if (down & HidNpadButton_A) return;
    if (can_reboot && (down & HidNpadButton_Y)) {
      printf("Rebooting...\n");
      consoleUpdate(NULL);
      Result rc = spsmInitialize();
      if (R_SUCCEEDED(rc)) {
        (void)spsmShutdown(true);
        spsmExit();
      }
      *quit_app = true;
      return;
    }
    if (down & HidNpadButton_Plus) {
      *quit_app = true;
      return;
    }
    consoleUpdate(NULL);
  }
  *quit_app = true;
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  consoleInit(NULL);
  const Result sock_rc = socketInitializeDefault();
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  Config cfg = load_config("sdmc:/switch/gba-sync/config.ini");

  std::vector<std::string> logs;
  bool quit_app = false;
  if (R_FAILED(sock_rc)) {
    logs.push_back("ERROR: socket init failed");
  } else if (cfg.server_url.empty()) {
    logs.push_back("ERROR: missing [server].url in config.ini");
  } else if (cfg.server_url.rfind("http://", 0) != 0) {
    logs.push_back("ERROR: use http:// URL for Switch MVP");
  } else {
    while (appletMainLoop() && !quit_app) {
      consoleClear();
      printf("GBA Sync (Switch MVP)\n");
      printf("---------------------\n");
      printf("Server: %s\n", cfg.server_url.c_str());
      printf("Save dir: %s\n", cfg.save_dir.c_str());
      printf("\n");
      sync_status_print_menu(cfg);
      {
        constexpr int kMenuLeftCol = 20;
        printf("%-*s%s\n", kMenuLeftCol, "A: Auto sync", "R: save viewer");
        printf("%-*s%s\n", kMenuLeftCol, "X: upload only", "-: Dropbox sync");
        printf("%-*s%s\n", kMenuLeftCol, "Y: download only", "+: exit app");
      }
      printf("\n");

      SyncAction action = SyncAction::Auto;
      if (!choose_action(&pad, &action)) {
        quit_app = true;
        break;
      }

      SyncManualFilter xy{};
      xy.all = true;
      std::vector<std::string> sync_logs;
      if (action == SyncAction::DropboxSync) {
        printf("\nDropbox sync now...\n");
        consoleUpdate(NULL);
        sync_logs = run_dropbox_sync_once(cfg);
      } else if (action == SyncAction::SaveViewer) {
        save_viewer_switch(&pad, cfg);
        continue;
      } else if (action == SyncAction::UploadOnly) {
        if (!pick_upload_selection(&pad, cfg, &xy)) continue;
        sync_logs = run_sync(cfg, action, &xy, &pad, nullptr);
      } else if (action == SyncAction::DownloadOnly) {
        if (!pick_download_selection(&pad, cfg, &xy)) continue;
        sync_logs = run_sync(cfg, action, &xy, &pad, nullptr);
      } else {
        bool already_up_to_date = false;
        sync_logs = run_sync(cfg, action, nullptr, &pad, &already_up_to_date);
        for (const auto& line : sync_logs) printf("%s\n", line.c_str());
        wait_after_sync_switch(&pad, &quit_app, already_up_to_date);
        continue;
      }
      for (const auto& line : sync_logs) printf("%s\n", line.c_str());
      wait_after_sync_switch(&pad, &quit_app, false);
    }
  }
  for (const auto& line : logs) printf("%s\n", line.c_str());
  if (!quit_app) {
    printf("\nPress + to exit.\n");

    while (appletMainLoop()) {
      padUpdate(&pad);
      if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
      consoleUpdate(NULL);
    }
  }

  socketExit();
  consoleExit(NULL);
  return 0;
}
