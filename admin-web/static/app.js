const API = "/admin/api";

/** @type {any[]} */
let _savesCache = [];

async function api(path, opts = {}) {
  const r = await fetch(API + path, {
    credentials: "include",
    headers: { "Content-Type": "application/json", ...(opts.headers || {}) },
    ...opts,
  });
  const text = await r.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = { raw: text };
  }
  if (!r.ok) {
    const err = new Error(data?.detail || data?.raw || r.statusText);
    err.status = r.status;
    err.data = data;
    throw err;
  }
  return data;
}

/** ISO / RFC3339 timestamps → local, human-readable 12h time (hover for original). */
function formatAdminTime(iso) {
  if (iso == null || iso === "") return "";
  const s = String(iso).trim();
  if (!s) return "";
  const d = new Date(s);
  if (Number.isNaN(d.getTime())) return s;
  return d.toLocaleString(undefined, {
    month: "short",
    day: "numeric",
    year: "numeric",
    hour: "numeric",
    minute: "2-digit",
    second: "2-digit",
    hour12: true,
  });
}

function show(el, on) {
  el.classList.toggle("hidden", !on);
}

function setGlobal(msg, isErr) {
  const g = document.getElementById("global-msg");
  g.textContent = msg || "";
  g.classList.toggle("error", !!isErr);
  g.classList.toggle("has-msg", Boolean(msg && String(msg).trim()));
}

/** @param {Uint8Array} bytes */
async function sha256Hex(bytes) {
  const hash = await crypto.subtle.digest("SHA-256", bytes);
  return Array.from(new Uint8Array(hash))
    .map((b) => b.toString(16).padStart(2, "0"))
    .join("");
}

/**
 * Replace server save with file bytes (same as client PUT /save with force).
 * Backs up prior revision to history when the blob changes.
 */
async function uploadSaveFromFile(gameId, file) {
  const buf = await file.arrayBuffer();
  const bytes = new Uint8Array(buf);
  const size = bytes.byteLength;
  const t = file.lastModified ? file.lastModified : Date.now();
  const lastMod = new Date(t).toISOString().replace(/\.\d{3}Z$/, "Z");
  const qs = new URLSearchParams({
    last_modified_utc: lastMod,
    size_bytes: String(size),
    force: "true",
    platform_source: "admin-web",
  });
  let shaClient = "";
  if (typeof crypto !== "undefined" && crypto.subtle) {
    shaClient = await sha256Hex(bytes);
    qs.set("sha256", shaClient);
  }
  if (file.name) qs.set("filename_hint", file.name);
  const r = await fetch(`${API}/save/${encodeURIComponent(gameId)}?${qs}`, {
    method: "PUT",
    credentials: "include",
    headers: { "Content-Type": "application/octet-stream" },
    body: bytes,
  });
  const text = await r.text();
  let data = null;
  try {
    data = text ? JSON.parse(text) : null;
  } catch {
    data = { raw: text };
  }
  if (!r.ok) {
    const detail = data?.detail || data?.raw || r.statusText;
    throw new Error(typeof detail === "string" ? detail : JSON.stringify(detail));
  }
  const shaOut =
    (data && data.effective_meta && data.effective_meta.sha256) || shaClient || "";
  return {
    data,
    sha256: String(shaOut).toLowerCase(),
    sizeBytes: size,
    fileName: file.name ? String(file.name) : "",
  };
}

function showUploadSuccessModal(meta) {
  const { gameId, fileName, sizeBytes, sha256 } = meta;
  const modal = document.getElementById("upload-success-modal");
  const shaDisp = sha256 ? `${sha256.slice(0, 20)}…` : "—";
  const sizeDisp = sizeBytes != null ? `${Number(sizeBytes).toLocaleString()} bytes` : "—";
  if (!modal) {
    window.alert(
      `Upload complete.\n\ngame_id: ${gameId || "—"}\nFile: ${fileName || "—"}\nSize: ${sizeDisp}\nSHA256: ${shaDisp}`
    );
    return;
  }
  document.getElementById("upload-success-gameid").textContent = gameId || "—";
  const fnRow = document.getElementById("upload-success-filename-row");
  if (fileName) {
    document.getElementById("upload-success-filename").textContent = fileName;
    fnRow.classList.remove("hidden");
  } else {
    fnRow.classList.add("hidden");
  }
  document.getElementById("upload-success-size").textContent = sizeDisp;
  const shaEl = document.getElementById("upload-success-sha");
  shaEl.textContent = shaDisp;
  shaEl.title = sha256 || "";
  modal.classList.remove("hidden");
  document.getElementById("upload-success-close")?.focus();
}

function closeUploadSuccessModal() {
  document.getElementById("upload-success-modal")?.classList.add("hidden");
}

let _saveUploadInput = null;
function triggerSaveFileUpload(gameId) {
  if (!_saveUploadInput) {
    _saveUploadInput = document.createElement("input");
    _saveUploadInput.type = "file";
    _saveUploadInput.accept = ".sav,application/octet-stream";
    _saveUploadInput.className = "sr-only-file";
    _saveUploadInput.setAttribute("aria-hidden", "true");
    _saveUploadInput.addEventListener("change", async () => {
      const file = _saveUploadInput.files?.[0];
      const gid = _saveUploadInput.dataset.gameId || "";
      _saveUploadInput.value = "";
      if (!gid) {
        setGlobal("Upload failed: missing game id (please reload the page).", true);
        return;
      }
      if (!file) {
        return;
      }
      try {
        setGlobal("Uploading…", false);
        const meta = await uploadSaveFromFile(gid, file);
        showUploadSuccessModal({
          gameId: gid,
          fileName: meta.fileName,
          sizeBytes: meta.sizeBytes,
          sha256: meta.sha256,
        });
        setGlobal("");
        try {
          await loadAll();
        } catch (reloadErr) {
          setGlobal(
            `Upload saved, but refreshing the list failed: ${reloadErr.message || String(reloadErr)}`,
            true
          );
        }
      } catch (e) {
        setGlobal(e.message || String(e), true);
      }
    });
    document.body.appendChild(_saveUploadInput);
  }
  _saveUploadInput.dataset.gameId = gameId;
  _saveUploadInput.value = "";
  _saveUploadInput.click();
}

async function refreshAuth() {
  const me = await api("/me");
  const loginPanel = document.getElementById("login-panel");
  const main = document.getElementById("app-main");
  if (!me.admin_enabled) {
    show(loginPanel, false);
    show(main, false);
    setGlobal("Admin UI is disabled (set GBASYNC_ADMIN_PASSWORD on the server).", true);
    return;
  }
  if (me.misconfigured) {
    show(loginPanel, false);
    show(main, false);
    setGlobal("Admin misconfigured: set API_KEY or GBASYNC_ADMIN_SECRET.", true);
    return;
  }
  setGlobal("");
  if (me.authenticated) {
    show(loginPanel, false);
    show(main, true);
    await loadAll();
  } else {
    show(loginPanel, true);
    show(main, false);
  }
}

function attachSaveRowActions(wrap, s) {
  const bUpload = document.createElement("button");
  bUpload.textContent = "Upload";
  bUpload.type = "button";
  bUpload.title =
    "Replace the server save with a .sav from disk (current file is backed up to history; Dropbox sync may run). Same pipeline as a client upload with force.";
  bUpload.onclick = () => triggerSaveFileUpload(s.game_id);
  const bDl = document.createElement("button");
  bDl.textContent = "Download";
  bDl.type = "button";
  bDl.onclick = () => downloadSave(s.game_id);
  const bHist = document.createElement("button");
  bHist.textContent = "History";
  bHist.type = "button";
  bHist.onclick = () => openHistoryModal(s.game_id);
  const bName = document.createElement("button");
  bName.textContent = "Display name";
  bName.type = "button";
  bName.title = "Name for the current save on the server (index only; not a history backup)";
  bName.onclick = () => editDisplayName(s.game_id, s.display_name || "");
  let bResolve = null;
  if (s.conflict) {
    bResolve = document.createElement("button");
    bResolve.textContent = "Resolve";
    bResolve.type = "button";
    bResolve.onclick = () => {
      document.getElementById("resolve-id").value = s.game_id;
      switchTab("actions");
    };
  }

  /* 2×2 grid (table + cards): row1 Download | Upload, row2 History | Display name */
  const grid = document.createElement("div");
  grid.className = "save-actions-grid";
  grid.appendChild(bDl);
  grid.appendChild(bUpload);
  grid.appendChild(bHist);
  grid.appendChild(bName);
  wrap.appendChild(grid);
  if (bResolve) {
    const row = document.createElement("div");
    row.className = "save-actions-resolve";
    row.appendChild(bResolve);
    wrap.appendChild(row);
  }
}

function renderSavesCards(list) {
  const root = document.getElementById("saves-cards");
  if (!root) return;
  root.innerHTML = "";
  for (const s of list) {
    const card = document.createElement("article");
    card.className = "save-card";
    const actions = document.createElement("div");
    actions.className = "save-card-actions";
    attachSaveRowActions(actions, s);
    const body = document.createElement("div");
    body.className = "save-card-body";
    const shaFull = s.sha256 || "";
    const shaShort = shaFull.slice(0, 12) + "…";
    const dn = (s.display_name || "").trim();
    const gid = s.game_id || "";
    const cardTitle = dn || gid || "—";
    const showGameIdLine = Boolean(dn && gid);
    body.innerHTML = `
      <h3 class="save-card-title">${escapeHtml(cardTitle)}</h3>
      ${showGameIdLine ? `<p class="save-card-gameid"><span class="save-card-gameid-label">game_id</span><span class="save-card-gameid-value mono">${escapeHtml(gid)}</span></p>` : ""}
      <dl class="save-card-dl">
        <div><dt>SHA256</dt><dd class="mono" title="${escapeHtml(shaFull)}">${escapeHtml(shaShort)}</dd></div>
        <div><dt>Size</dt><dd>${escapeHtml(String(s.size_bytes ?? ""))}</dd></div>
        <div><dt>Conflict</dt><dd>${s.conflict ? "yes" : "—"}</dd></div>
        <div><dt>Modified</dt><dd title="${escapeHtml(s.last_modified_utc || "")}">${escapeHtml(formatAdminTime(s.last_modified_utc))}</dd></div>
        <div><dt>Server upload</dt><dd title="${escapeHtml(s.server_updated_at || "")}">${escapeHtml(formatAdminTime(s.server_updated_at)) || "—"}</dd></div>
      </dl>`;
    card.appendChild(actions);
    card.appendChild(body);
    root.appendChild(card);
  }
}

function renderSavesTable() {
  const q = (document.getElementById("saves-filter")?.value || "").trim().toLowerCase();
  const tb = document.querySelector("#tbl-saves tbody");
  tb.innerHTML = "";
  const list = _savesCache.filter((s) => {
    if (!q) return true;
    const id = (s.game_id || "").toLowerCase();
    const dn = (s.display_name || "").toLowerCase();
    return id.includes(q) || dn.includes(q);
  });
  list.sort((a, b) => (a.list_order ?? 0) - (b.list_order ?? 0));
  renderSavesCards(list);
  for (const s of list) {
    const tr = document.createElement("tr");
    tr.dataset.gameId = s.game_id || "";
    tr.draggable = !q;
    const shaFull = s.sha256 || "";
    const shaShort = shaFull.slice(0, 12) + "…";
    const dragHint = q ? "" : "Drag to reorder";
    const dragMark = q ? "" : "⋮";
    tr.innerHTML = `<td class="drag-cell" title="${dragHint}">${dragMark}</td><td>${escapeHtml(s.display_name || "")}</td><td class="mono">${escapeHtml(s.game_id)}</td><td title="${escapeHtml(shaFull)}" class="mono"><code>${escapeHtml(shaShort)}</code></td><td>${s.size_bytes ?? ""}</td><td>${s.conflict ? "yes" : ""}</td><td title="${escapeHtml(s.last_modified_utc || "")}">${escapeHtml(formatAdminTime(s.last_modified_utc))}</td><td title="${escapeHtml(s.server_updated_at || "")}">${escapeHtml(formatAdminTime(s.server_updated_at)) || "—"}</td><td class="row-actions row-actions-saves"></td>`;
    const wrap = tr.querySelector(".row-actions");
    attachSaveRowActions(wrap, s);
    tb.appendChild(tr);
  }
}

function wireSavesTableDnD() {
  const tb = document.querySelector("#tbl-saves tbody");
  if (!tb || tb.dataset.dndWired) return;
  tb.dataset.dndWired = "1";
  let dragRow = null;
  tb.addEventListener("dragstart", (e) => {
    const tr = e.target.closest("tr");
    if (!tr || !tr.draggable) return;
    dragRow = tr;
    e.dataTransfer.effectAllowed = "move";
    try {
      e.dataTransfer.setData("text/plain", tr.dataset.gameId || "");
    } catch (_) {}
  });
  tb.addEventListener("dragend", () => {
    dragRow = null;
  });
  tb.addEventListener("dragover", (e) => {
    if (!dragRow) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = "move";
  });
  tb.addEventListener("drop", (e) => {
    e.preventDefault();
    const tr = e.target.closest("tr");
    if (!tr || !dragRow || tr === dragRow) return;
    const rect = tr.getBoundingClientRect();
    const before = e.clientY < rect.top + rect.height / 2;
    if (before) tb.insertBefore(dragRow, tr);
    else tb.insertBefore(dragRow, tr.nextSibling);
  });
}

async function downloadSave(id) {
  const r = await fetch(API + "/save/" + encodeURIComponent(id) + "/download", { credentials: "include" });
  if (!r.ok) {
    const t = await r.text();
    setGlobal(t || r.statusText, true);
    return;
  }
  const blob = await r.blob();
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = id + ".sav";
  a.click();
  URL.revokeObjectURL(a.href);
}

async function openHistoryModal(gameId) {
  const h = await api("/save/" + encodeURIComponent(gameId) + "/history");
  document.getElementById("history-modal-title").textContent = "History: " + gameId;
  const root = document.getElementById("history-entries");
  root.innerHTML = "";
  for (const e of h.entries || []) {
    const label = e.display_name || "";
    const keepOn = !!e.keep;
    const card = document.createElement("article");
    card.className = "history-entry-card";
    card.setAttribute("role", "listitem");
    const meta = document.createElement("div");
    meta.className = "history-entry-meta";
    meta.innerHTML = `
      <div class="history-entry-row"><span class="history-entry-k">Label</span><span class="history-entry-v">${label ? escapeHtml(label) : "—"}</span></div>
      <div class="history-entry-row"><span class="history-entry-k">File</span><span class="history-entry-v mono">${escapeHtml(e.filename)}</span></div>
      <div class="history-entry-row"><span class="history-entry-k">Size</span><span class="history-entry-v">${escapeHtml(String(e.size_bytes ?? ""))}</span></div>
      <div class="history-entry-row"><span class="history-entry-k">Modified</span><span class="history-entry-v" title="${escapeHtml(e.modified_utc || "")}">${escapeHtml(formatAdminTime(e.modified_utc))}</span></div>
      <div class="history-entry-row"><span class="history-entry-k">Index time</span><span class="history-entry-v" title="${escapeHtml(e.indexed_at_utc || "")}">${e.indexed_at_utc ? escapeHtml(formatAdminTime(e.indexed_at_utc)) : "—"}</span></div>
      <div class="history-entry-row"><span class="history-entry-k">Keep</span><span class="history-entry-v">${keepOn ? "yes (not purged)" : "no"}</span></div>`;
    const actions = document.createElement("div");
    actions.className = "history-entry-actions";
    const bRestore = document.createElement("button");
    bRestore.textContent = "Restore";
    bRestore.type = "button";
    bRestore.className = "btn-primary";
    bRestore.onclick = async () => {
      if (!confirm("Restore this revision on the server? The current file will be backed up to history first.")) return;
      try {
        await api("/save/" + encodeURIComponent(gameId) + "/restore", {
          method: "POST",
          body: JSON.stringify({ filename: e.filename }),
        });
        setGlobal("Restored.", false);
        closeHistoryModal();
        await loadAll();
      } catch (x) {
        setGlobal(x.message || String(x), true);
      }
    };
    actions.appendChild(bRestore);
    const bKeep = document.createElement("button");
    bKeep.textContent = keepOn ? "Unkeep" : "Keep";
    bKeep.type = "button";
    bKeep.className = "btn-secondary";
    bKeep.title =
      "Pin this backup so it is never deleted when history exceeds the max count (unpinned backups are trimmed).";
    bKeep.onclick = async () => {
      try {
        await api("/save/" + encodeURIComponent(gameId) + "/history/revision/keep", {
          method: "PATCH",
          body: JSON.stringify({ filename: e.filename, keep: !keepOn }),
        });
        setGlobal(keepOn ? "Keep removed." : "Keep enabled for this backup.", false);
        await openHistoryModal(gameId);
      } catch (x) {
        setGlobal(x.message || String(x), true);
      }
    };
    actions.appendChild(bKeep);
    const bLabel = document.createElement("button");
    bLabel.textContent = "Assign Label";
    bLabel.type = "button";
    bLabel.title = "Label for this history file only (stored in labels.json)";
    bLabel.onclick = async () => {
      const v = prompt("Label for this history backup only (empty to clear)", label);
      if (v === null) return;
      try {
        await api("/save/" + encodeURIComponent(gameId) + "/history/revision", {
          method: "PATCH",
          body: JSON.stringify({ filename: e.filename, display_name: v.trim() || null }),
        });
        setGlobal("Revision label saved.", false);
        await openHistoryModal(gameId);
      } catch (x) {
        setGlobal(x.message || String(x), true);
      }
    };
    actions.appendChild(bLabel);
    card.appendChild(meta);
    card.appendChild(actions);
    root.appendChild(card);
  }
  document.getElementById("history-modal").classList.remove("hidden");
}

function closeHistoryModal() {
  document.getElementById("history-modal").classList.add("hidden");
}

async function editDisplayName(gameId, current) {
  const v = prompt("Display name for this save (shown in admin + clients; empty to clear)", current);
  if (v === null) return;
  try {
    await api("/save/" + encodeURIComponent(gameId) + "/meta", {
      method: "PATCH",
      body: JSON.stringify({ display_name: v.trim() || null }),
    });
    await loadAll();
  } catch (x) {
    setGlobal(x.message || String(x), true);
  }
}

const ROUTING_PLACEHOLDERS = {
  aliases: ["legacy or alternate game_id", "canonical game_id"],
  rom: ["40 hex chars (ROM SHA-1)", "canonical game_id"],
  tomb: ["retired game_id", "canonical game_id"],
};

function routingTbodyId(kind) {
  if (kind === "aliases") return "routing-tbody-aliases";
  if (kind === "rom") return "routing-tbody-rom";
  return "routing-tbody-tomb";
}

function appendRoutingRow(tb, k, v, keyPh, valPh) {
  const tr = document.createElement("tr");
  const tdK = document.createElement("td");
  const inpK = document.createElement("input");
  inpK.type = "text";
  inpK.className = "r-key mono";
  inpK.spellcheck = false;
  inpK.autocomplete = "off";
  inpK.placeholder = keyPh;
  inpK.value = k;
  const tdV = document.createElement("td");
  const inpV = document.createElement("input");
  inpV.type = "text";
  inpV.className = "r-val mono";
  inpV.spellcheck = false;
  inpV.autocomplete = "off";
  inpV.placeholder = valPh;
  inpV.value = v;
  const tdA = document.createElement("td");
  tdA.className = "routing-cell-actions";
  const rem = document.createElement("button");
  rem.type = "button";
  rem.textContent = "Remove";
  rem.className = "routing-remove";
  rem.addEventListener("click", () => tr.remove());
  tdK.appendChild(inpK);
  tdV.appendChild(inpV);
  tdA.appendChild(rem);
  tr.appendChild(tdK);
  tr.appendChild(tdV);
  tr.appendChild(tdA);
  tb.appendChild(tr);
}

function fillRoutingTbody(tbodyId, map, keyPh, valPh) {
  const tb = document.getElementById(tbodyId);
  tb.innerHTML = "";
  const entries = Object.entries(map || {}).sort(([a], [b]) => a.localeCompare(b));
  if (entries.length === 0) {
    appendRoutingRow(tb, "", "", keyPh, valPh);
  } else {
    for (const [k, v] of entries) {
      appendRoutingRow(tb, k, v, keyPh, valPh);
    }
  }
}

function renderRouting(idx) {
  const a = ROUTING_PLACEHOLDERS.aliases;
  const r = ROUTING_PLACEHOLDERS.rom;
  const t = ROUTING_PLACEHOLDERS.tomb;
  fillRoutingTbody("routing-tbody-aliases", idx.aliases, a[0], a[1]);
  fillRoutingTbody("routing-tbody-rom", idx.rom_sha1, r[0], r[1]);
  fillRoutingTbody("routing-tbody-tomb", idx.tombstones, t[0], t[1]);
}

function collectRoutingMap(kind) {
  const tb = document.getElementById(routingTbodyId(kind));
  const out = {};
  tb.querySelectorAll("tr").forEach((tr) => {
    const k = tr.querySelector(".r-key")?.value?.trim();
    const v = tr.querySelector(".r-val")?.value?.trim();
    if (k && v) out[k] = v;
  });
  return out;
}

function renderDashboard(dash) {
  document.getElementById("dash-summary").textContent = dash.summary || "";
  document.getElementById("dash-save-count").textContent = dash.save_count != null ? String(dash.save_count) : "—";
  document.getElementById("dash-conflict-count").textContent = dash.conflict_count != null ? String(dash.conflict_count) : "—";
  document.getElementById("dash-dropbox-mode").textContent = dash.dropbox_mode ?? "—";
  document.getElementById("dash-index-path").textContent = dash.index_path || "";
  document.getElementById("dash-save-root").textContent = dash.save_root || "";
  document.getElementById("dash-history-root").textContent = dash.history_root || "";
  const hm = document.getElementById("setting-history-max");
  if (hm && dash.history_max_versions_per_game != null) hm.value = String(dash.history_max_versions_per_game);
}

async function loadAll() {
  const dash = await api("/dashboard");
  renderDashboard(dash);

  const saves = await api("/saves");
  _savesCache = saves.saves || [];
  renderSavesTable();
  wireSavesTableDnD();

  const conf = await api("/conflicts");
  const tbC = document.querySelector("#tbl-conflicts tbody");
  tbC.innerHTML = "";
  for (const s of conf.saves || []) {
    const tr = document.createElement("tr");
    const sha = (s.sha256 || "").slice(0, 12) + "…";
    tr.innerHTML = `<td>${escapeHtml(s.game_id)}</td><td><code>${escapeHtml(sha)}</code></td><td class="row-actions"></td>`;
    const wrap = tr.querySelector(".row-actions");
    const b = document.createElement("button");
    b.textContent = "Resolve";
    b.type = "button";
    b.onclick = () => {
      document.getElementById("resolve-id").value = s.game_id;
      switchTab("actions");
    };
    wrap.appendChild(b);
    tbC.appendChild(tr);
  }

  const idx = await api("/index-state");
  renderRouting(idx);

  const slot = await api("/slot-map");
  const sm = document.getElementById("slot-meta");
  if (!slot.configured) {
    sm.textContent = "Set GBASYNC_SLOT_MAP_PATH to a host path for the Delta slot map JSON (optional).";
    document.getElementById("slot-json").textContent = "";
  } else {
    sm.textContent = slot.path + (slot.error ? " — " + slot.error : "");
    document.getElementById("slot-json").textContent = slot.json != null ? JSON.stringify(slot.json, null, 2) : "";
  }
}

function escapeHtml(s) {
  const d = document.createElement("div");
  d.textContent = s;
  return d.innerHTML;
}

function switchTab(name) {
  document.querySelectorAll(".tabs button[data-tab]").forEach((b) => {
    b.classList.toggle("active", b.getAttribute("data-tab") === name);
  });
  document.querySelectorAll(".tab-pane").forEach((p) => p.classList.add("hidden"));
  const map = {
    dash: "tab-dash",
    saves: "tab-saves",
    conflicts: "tab-conflicts",
    routing: "tab-routing",
    slot: "tab-slot",
    actions: "tab-actions",
  };
  const id = map[name];
  if (id) document.getElementById(id).classList.remove("hidden");
}

document.querySelectorAll(".tabs button[data-tab]").forEach((btn) => {
  btn.addEventListener("click", () => switchTab(btn.getAttribute("data-tab")));
});

document.getElementById("login-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const pw = document.getElementById("login-password").value;
  const err = document.getElementById("login-error");
  err.textContent = "";
  try {
    await api("/login", { method: "POST", body: JSON.stringify({ password: pw }) });
    document.getElementById("login-password").value = "";
    await refreshAuth();
  } catch (x) {
    err.textContent = x.message || "Login failed";
  }
});

document.getElementById("btn-logout").addEventListener("click", async () => {
  await api("/logout", { method: "POST" });
  await refreshAuth();
});

document.getElementById("btn-dropbox").addEventListener("click", async () => {
  const el = document.getElementById("dropbox-msg");
  el.textContent = "Running…";
  el.classList.remove("ok");
  try {
    const r = await api("/dropbox/sync-once", { method: "POST" });
    el.textContent = JSON.stringify(r);
    el.classList.add("ok");
  } catch (x) {
    el.textContent = x.message;
  }
});

document.getElementById("btn-resolve").addEventListener("click", async () => {
  const id = document.getElementById("resolve-id").value.trim();
  const el = document.getElementById("resolve-msg");
  el.textContent = "";
  if (!id) return;
  try {
    const r = await api("/resolve/" + encodeURIComponent(id), { method: "POST" });
    el.textContent = JSON.stringify(r);
    el.classList.add("ok");
    await loadAll();
  } catch (x) {
    el.textContent = x.message;
  }
});

document.getElementById("btn-delete").addEventListener("click", async () => {
  const id = document.getElementById("delete-id").value.trim();
  const c = document.getElementById("delete-confirm").value.trim();
  const el = document.getElementById("delete-msg");
  el.textContent = "";
  if (!id || id !== c) {
    el.textContent = "game_id and confirmation must match.";
    return;
  }
  if (!confirm("Delete save " + id + " from the server index and disk?")) return;
  try {
    const r = await api("/save/" + encodeURIComponent(id), { method: "DELETE" });
    el.textContent = JSON.stringify(r);
    el.classList.add("ok");
    document.getElementById("delete-id").value = "";
    document.getElementById("delete-confirm").value = "";
    await loadAll();
  } catch (x) {
    el.textContent = x.message;
  }
});

document.getElementById("saves-filter")?.addEventListener("input", () => renderSavesTable());

document.getElementById("btn-save-order")?.addEventListener("click", async () => {
  const q = (document.getElementById("saves-filter")?.value || "").trim();
  if (q) {
    setGlobal("Clear the search filter before saving order (all saves must be visible).", true);
    return;
  }
  const tb = document.querySelector("#tbl-saves tbody");
  const ids = [...tb.querySelectorAll("tr")].map((tr) => tr.dataset.gameId).filter(Boolean);
  if (ids.length === 0) return;
  try {
    await api("/save-order", { method: "PUT", body: JSON.stringify({ game_ids: ids }) });
    setGlobal("Save order saved.", false);
    await loadAll();
  } catch (x) {
    setGlobal(x.message || String(x), true);
  }
});
document.getElementById("history-close")?.addEventListener("click", () => closeHistoryModal());
document.getElementById("history-modal")?.addEventListener("click", (e) => {
  if (e.target.classList.contains("modal-backdrop")) closeHistoryModal();
});

document.getElementById("upload-success-close")?.addEventListener("click", () => closeUploadSuccessModal());
document.getElementById("upload-success-modal")?.addEventListener("click", (e) => {
  if (e.target.classList.contains("modal-backdrop")) closeUploadSuccessModal();
});

document.getElementById("btn-save-settings")?.addEventListener("click", async () => {
  const el = document.getElementById("dash-settings-msg");
  el.textContent = "";
  el.classList.remove("ok");
  const raw = document.getElementById("setting-history-max").value.trim();
  const n = parseInt(raw, 10);
  if (raw === "" || Number.isNaN(n) || n < 0) {
    el.textContent = "Enter a non-negative integer.";
    return;
  }
  if (n > 1000000) {
    el.textContent = "Value must be at most 1,000,000.";
    return;
  }
  try {
    await api("/settings", { method: "PATCH", body: JSON.stringify({ history_max_versions_per_game: n }) });
    el.textContent = "Saved.";
    el.classList.add("ok");
    const d = await api("/dashboard");
    renderDashboard(d);
  } catch (x) {
    el.textContent = x.message || String(x);
  }
});

document.getElementById("btn-save-routing")?.addEventListener("click", async () => {
  const el = document.getElementById("routing-msg");
  el.textContent = "";
  el.classList.remove("ok");
  try {
    await api("/index-routing", {
      method: "PUT",
      body: JSON.stringify({
        aliases: collectRoutingMap("aliases"),
        rom_sha1: collectRoutingMap("rom"),
        tombstones: collectRoutingMap("tomb"),
      }),
    });
    el.textContent = "Saved.";
    el.classList.add("ok");
    const idx = await api("/index-state");
    renderRouting(idx);
    const dash = await api("/dashboard");
    renderDashboard(dash);
  } catch (x) {
    el.textContent = x.message || String(x);
  }
});

document.getElementById("tab-routing")?.addEventListener("click", (e) => {
  const btn = e.target.closest(".routing-add[data-routing-kind]");
  if (!btn) return;
  const kind = btn.getAttribute("data-routing-kind");
  const ph = ROUTING_PLACEHOLDERS[kind];
  if (!ph) return;
  const tb = document.getElementById(routingTbodyId(kind));
  appendRoutingRow(tb, "", "", ph[0], ph[1]);
});

refreshAuth().catch((e) => {
  setGlobal(e.message || String(e), true);
});
