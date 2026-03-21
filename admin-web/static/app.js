const API = "/admin/api";

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

function show(el, on) {
  el.classList.toggle("hidden", !on);
}

function setGlobal(msg, isErr) {
  const g = document.getElementById("global-msg");
  g.textContent = msg || "";
  g.classList.toggle("error", !!isErr);
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

async function loadAll() {
  const dash = await api("/dashboard");
  document.getElementById("dash-json").textContent = JSON.stringify(dash, null, 2);

  const saves = await api("/saves");
  const tb = document.querySelector("#tbl-saves tbody");
  tb.innerHTML = "";
  for (const s of saves.saves || []) {
    const tr = document.createElement("tr");
    const sha = (s.sha256 || "").slice(0, 12) + "…";
    tr.innerHTML = `<td>${escapeHtml(s.game_id)}</td><td><code>${escapeHtml(sha)}</code></td><td>${s.size_bytes ?? ""}</td><td>${s.conflict ? "yes" : ""}</td><td>${escapeHtml(s.last_modified_utc || "")}</td><td class="row-actions"></td>`;
    const wrap = tr.querySelector(".row-actions");
    const b = document.createElement("button");
    b.textContent = "Resolve";
    b.type = "button";
    b.onclick = () => {
      document.getElementById("resolve-id").value = s.game_id;
      switchTab("actions");
    };
    wrap.appendChild(b);
    tb.appendChild(tr);
  }

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
    tb.appendChild(tr);
  }

  const idx = await api("/index-state");
  document.getElementById("routing-json").textContent = JSON.stringify(idx, null, 2);

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

refreshAuth().catch((e) => {
  setGlobal(e.message || String(e), true);
});
