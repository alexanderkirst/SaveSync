#include <switch.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <netdb.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

struct Config {
  std::string server_url;
  std::string api_key;
  std::string save_dir = "sdmc:/roms/gba/saves";
};

struct SaveMeta {
  std::string game_id;
  std::string last_modified_utc;
  std::string filename_hint;
};

struct LocalSave {
  std::string path;
  std::string name;
  std::string game_id;
  std::string last_modified_utc;
  std::string sha256;
  size_t size_bytes = 0;
};

static std::string trim(const std::string& input) {
  const auto start = input.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) return "";
  const auto end = input.find_last_not_of(" \t\r\n");
  return input.substr(start, end - start + 1);
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
  }
  return cfg;
}

static std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
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
  return std::rename(tmp.c_str(), path.c_str()) == 0;
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

static bool http_request(
    const Config& cfg,
    const std::string& method,
    const std::string& target_path,
    const std::vector<unsigned char>& body,
    int& out_status,
    std::vector<unsigned char>& out_body) {
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

  std::ostringstream request;
  request << method << " " << target_path << " HTTP/1.1\r\n";
  request << "Host: " << parsed.host << "\r\n";
  request << "X-API-Key: " << cfg.api_key << "\r\n";
  request << "Content-Type: application/octet-stream\r\n";
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
  out_body.assign(response.begin() + static_cast<long>(body_pos + 4), response.end());
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

static std::vector<LocalSave> scan_local_saves(const std::string& dir) {
  std::vector<LocalSave> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  dirent* ent = nullptr;
  while ((ent = readdir(d)) != nullptr) {
    std::string name(ent->d_name);
    if (name == "." || name == ".." || !has_sav_extension(name)) continue;
    LocalSave s{};
    s.name = name;
    s.path = dir + "/" + name;
    s.game_id = sanitize_game_id(file_stem(name));
    struct stat st{};
    if (stat(s.path.c_str(), &st) != 0) continue;
    s.last_modified_utc = mtime_to_utc_iso(st.st_mtime);
    std::vector<unsigned char> bytes = read_file(s.path);
    s.size_bytes = bytes.size();
    s.sha256 = sha256_impl::hash(bytes);
    out.push_back(s);
  }
  closedir(d);
  return out;
}

static std::vector<std::string> run_sync(const Config& cfg) {
  std::vector<std::string> logs;
  auto local = scan_local_saves(cfg.save_dir);
  std::map<std::string, LocalSave> local_by_id;
  for (const auto& s : local) local_by_id[s.game_id] = s;
  logs.push_back("Local saves: " + std::to_string(local.size()));

  int status = 0;
  std::vector<unsigned char> body;
  if (!http_request(cfg, "GET", "/saves", {}, status, body) || status != 200) {
    logs.push_back("ERROR: GET /saves failed");
    return logs;
  }
  std::string json(body.begin(), body.end());
  auto remote = parse_saves_json(json);
  logs.push_back("Remote saves: " + std::to_string(remote.size()));

  for (const auto& [id, l] : local_by_id) {
    const auto it = remote.find(id);
    if (it != remote.end() && l.last_modified_utc <= it->second.last_modified_utc) {
      logs.push_back(id + ": OK");
      continue;
    }
    const std::vector<unsigned char> bytes = read_file(l.path);
    std::ostringstream path;
    path << "/save/" << id
         << "?last_modified_utc=" << url_encode_simple(l.last_modified_utc)
         << "&sha256=" << url_encode_simple(l.sha256)
         << "&size_bytes=" << l.size_bytes
         << "&filename_hint=" << url_encode_simple(l.name)
         << "&platform_source=switch-homebrew";
    std::vector<unsigned char> put_resp;
    int put_status = 0;
    if (http_request(cfg, "PUT", path.str(), bytes, put_status, put_resp) && put_status == 200) {
      logs.push_back(id + ": UPLOADED");
    } else {
      logs.push_back(id + ": ERROR(upload)");
    }
  }

  body.clear();
  if (!http_request(cfg, "GET", "/saves", {}, status, body) || status != 200) {
    logs.push_back("ERROR: refresh GET /saves failed");
    return logs;
  }
  json.assign(body.begin(), body.end());
  remote = parse_saves_json(json);

  for (const auto& [id, r] : remote) {
    const auto it = local_by_id.find(id);
    if (it != local_by_id.end() && r.last_modified_utc <= it->second.last_modified_utc) continue;
    std::vector<unsigned char> save_data;
    int get_status = 0;
    if (!http_request(cfg, "GET", "/save/" + id, {}, get_status, save_data) || get_status != 200) {
      logs.push_back(id + ": ERROR(download)");
      continue;
    }
    std::string target_name = r.filename_hint.empty() ? id + ".sav" : r.filename_hint;
    if (!has_sav_extension(target_name)) target_name += ".sav";
    if (write_atomic(cfg.save_dir + "/" + target_name, save_data)) {
      logs.push_back(id + ": DOWNLOADED");
    } else {
      logs.push_back(id + ": ERROR(write)");
    }
  }
  return logs;
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  consoleInit(NULL);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);

  Config cfg = load_config("sdmc:/switch/gba-sync/config.ini");
  printf("GBA Sync (Switch MVP)\n");
  printf("---------------------\n");
  printf("Server: %s\n", cfg.server_url.empty() ? "(missing config)" : cfg.server_url.c_str());
  printf("Save dir: %s\n\n", cfg.save_dir.c_str());

  std::vector<std::string> logs;
  if (cfg.server_url.empty()) {
    logs.push_back("ERROR: missing [server].url in config.ini");
  } else if (cfg.server_url.rfind("http://", 0) != 0) {
    logs.push_back("ERROR: use http:// URL for Switch MVP");
  } else {
    logs = run_sync(cfg);
  }
  for (const auto& line : logs) printf("%s\n", line.c_str());
  printf("\nPress + to exit.\n");

  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(NULL);
  }

  consoleExit(NULL);
  return 0;
}
