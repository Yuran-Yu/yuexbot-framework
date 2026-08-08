const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const rootDir = __dirname;
const dataDir = path.join(rootDir, "data");
const configDir = path.join(rootDir, "config");
const publicDir = path.join(rootDir, "public");
const port = Number(process.env.YUEXBOT_SERVICE_PORT || process.env.PORT || 8787);
const adminToken = process.env.YUEXBOT_ADMIN_TOKEN || "";

const paths = {
  events: path.join(dataDir, "events.json"),
  clients: path.join(dataDir, "clients.json"),
  accounts: path.join(dataDir, "accounts.json"),
  plugins: path.join(dataDir, "plugins.json"),
  violations: path.join(dataDir, "violations.json"),
  releases: path.join(configDir, "releases.json"),
  rules: path.join(configDir, "plugin-rules.json")
};

function ensureStore() {
  fs.mkdirSync(dataDir, { recursive: true });
  fs.mkdirSync(configDir, { recursive: true });
  for (const file of [paths.events, paths.clients, paths.accounts, paths.plugins, paths.violations]) {
    if (!fs.existsSync(file)) fs.writeFileSync(file, "[]\n", "utf8");
  }
}

function readJson(file, fallback) {
  try {
    return JSON.parse(fs.readFileSync(file, "utf8"));
  } catch (_) {
    return fallback;
  }
}

function writeJson(file, data) {
  fs.writeFileSync(file, JSON.stringify(data, null, 2) + "\n", "utf8");
}

function appendJson(file, item, limit = 5000) {
  const list = readJson(file, []);
  list.unshift(item);
  if (list.length > limit) list.length = limit;
  writeJson(file, list);
}

function jsonResponse(res, code, data) {
  const body = JSON.stringify(data);
  res.writeHead(code, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
    "access-control-allow-origin": "*",
    "access-control-allow-methods": "GET,POST,OPTIONS",
    "access-control-allow-headers": "content-type,authorization,x-yuexbot-token"
  });
  res.end(body);
}

function textResponse(res, code, body, type = "text/plain; charset=utf-8") {
  res.writeHead(code, { "content-type": type, "cache-control": "no-store" });
  res.end(body);
}

function parseBody(req) {
  return new Promise((resolve) => {
    let raw = "";
    req.on("data", (chunk) => {
      raw += chunk;
      if (raw.length > 1024 * 1024) req.destroy();
    });
    req.on("end", () => {
      if (!raw) return resolve({});
      try {
        resolve(JSON.parse(raw));
      } catch (_) {
        resolve({ _invalidJson: true });
      }
    });
    req.on("error", () => resolve({}));
  });
}

function clientIdFrom(payload) {
  const raw = [
    payload.install_id,
    payload.machine_id,
    payload.client_id,
    payload.device_id,
    payload.framework_version || payload.version || "",
    payload.os || ""
  ].filter(Boolean).join("|");
  return crypto.createHash("sha256").update(raw || String(Date.now())).digest("hex").slice(0, 24);
}

function nowIso() {
  return new Date().toISOString();
}

function sanitizeAccount(account) {
  return {
    account_id: String(account.account_id || account.self_id || account.qq || ""),
    account_hash: hashText(account.qq || account.self_id || account.account_id || ""),
    display_name: String(account.display_name || account.account_name || account.name || ""),
    mode: String(account.mode || ""),
    status: String(account.status || ""),
    group_count: Number(account.group_count || 0),
    friend_count: Number(account.friend_count || 0),
    message_count: Number(account.message_count || 0),
    event_count: Number(account.event_count || 0)
  };
}

function hashText(value) {
  return crypto.createHash("sha256").update(String(value || "")).digest("hex");
}

function upsertByKey(file, key, item) {
  const list = readJson(file, []);
  const index = list.findIndex((row) => row[key] === item[key]);
  if (index >= 0) list[index] = { ...list[index], ...item };
  else list.unshift(item);
  writeJson(file, list);
  return item;
}

function compareVersions(current, latest) {
  const left = String(current || "0").split(".").map((v) => Number(v) || 0);
  const right = String(latest || "0").split(".").map((v) => Number(v) || 0);
  const len = Math.max(left.length, right.length);
  for (let i = 0; i < len; i += 1) {
    if ((left[i] || 0) < (right[i] || 0)) return -1;
    if ((left[i] || 0) > (right[i] || 0)) return 1;
  }
  return 0;
}

function selectRelease(channel) {
  const releases = readJson(paths.releases, { channels: {} });
  const channels = releases.channels || {};
  return channels[channel] || channels.stable || { version: "0.0.0", notes: [], url: "" };
}

function analyzePlugin(plugin) {
  const rules = readJson(paths.rules, { banned_ids: [], banned_keywords: [], suspicious_permissions: [] });
  const id = String(plugin.id || plugin.plugin_id || "");
  const name = String(plugin.name || "");
  const description = String(plugin.description || "");
  const permissions = Array.isArray(plugin.permissions) ? plugin.permissions.map(String) : [];
  const haystack = `${id}\n${name}\n${description}`.toLowerCase();
  const hits = [];

  for (const bannedId of rules.banned_ids || []) {
    if (id.toLowerCase() === String(bannedId).toLowerCase()) hits.push(`banned_id:${bannedId}`);
  }
  for (const keyword of rules.banned_keywords || []) {
    if (keyword && haystack.includes(String(keyword).toLowerCase())) hits.push(`keyword:${keyword}`);
  }
  for (const permission of rules.suspicious_permissions || []) {
    if (permissions.includes(permission)) hits.push(`permission:${permission}`);
  }

  const status = hits.length ? "blocked" : "clean";
  return { status, hits };
}

function requireAdmin(req, res) {
  if (!adminToken) return true;
  const auth = req.headers.authorization || "";
  const token = req.headers["x-yuexbot-token"] || auth.replace(/^Bearer\s+/i, "");
  if (token === adminToken) return true;
  jsonResponse(res, 401, { ok: false, error: "admin token required" });
  return false;
}

async function routeApi(req, res, url) {
  if (req.method === "OPTIONS") return jsonResponse(res, 204, {});

  if (req.method === "GET" && url.pathname === "/api/health") {
    return jsonResponse(res, 200, { ok: true, service: "yuexbot-service", version: "0.1.0", time: nowIso() });
  }

  if (req.method === "POST" && url.pathname === "/api/v1/update/check") {
    const body = await parseBody(req);
    if (body._invalidJson) return jsonResponse(res, 400, { ok: false, error: "invalid json" });
    const release = selectRelease(body.channel || "stable");
    return jsonResponse(res, 200, {
      ok: true,
      update_available: compareVersions(body.version, release.version) < 0,
      current_version: body.version || "",
      latest: release
    });
  }

  if (req.method === "POST" && url.pathname === "/api/v1/telemetry/framework") {
    const body = await parseBody(req);
    if (body._invalidJson) return jsonResponse(res, 400, { ok: false, error: "invalid json" });
    const client_id = clientIdFrom(body);
    const item = {
      client_id,
      version: String(body.framework_version || body.version || ""),
      channel: String(body.channel || "stable"),
      os: String(body.os || ""),
      arch: String(body.arch || ""),
      uptime: Number(body.uptime || 0),
      accounts: Number(body.accounts || 0),
      plugins: Number(body.plugins || 0),
      messages: Number(body.messages || 0),
      events: Number(body.events || 0),
      last_seen: nowIso()
    };
    upsertByKey(paths.clients, "client_id", item);
    appendJson(paths.events, { type: "framework", client_id, at: nowIso(), payload: item });
    return jsonResponse(res, 200, { ok: true, client_id });
  }

  if (req.method === "POST" && url.pathname === "/api/v1/telemetry/accounts") {
    const body = await parseBody(req);
    if (body._invalidJson) return jsonResponse(res, 400, { ok: false, error: "invalid json" });
    const client_id = clientIdFrom(body);
    const rows = Array.isArray(body.accounts) ? body.accounts.map(sanitizeAccount) : [];
    const item = { client_id, rows, reported_at: nowIso() };
    upsertByKey(paths.accounts, "client_id", item);
    appendJson(paths.events, { type: "accounts", client_id, at: nowIso(), count: rows.length });
    return jsonResponse(res, 200, { ok: true, count: rows.length });
  }

  if (req.method === "POST" && url.pathname === "/api/v1/telemetry/plugins") {
    const body = await parseBody(req);
    if (body._invalidJson) return jsonResponse(res, 400, { ok: false, error: "invalid json" });
    const client_id = clientIdFrom(body);
    const plugins = Array.isArray(body.plugins) ? body.plugins : [];
    const scanned = plugins.map((plugin) => {
      const result = analyzePlugin(plugin);
      return { ...plugin, compliance: result.status, hits: result.hits };
    });
    upsertByKey(paths.plugins, "client_id", { client_id, rows: scanned, reported_at: nowIso() });
    const violations = scanned.filter((plugin) => plugin.compliance !== "clean");
    for (const violation of violations) {
      appendJson(paths.violations, { client_id, at: nowIso(), plugin: violation });
    }
    appendJson(paths.events, { type: "plugins", client_id, at: nowIso(), count: scanned.length, violations: violations.length });
    return jsonResponse(res, 200, { ok: true, count: scanned.length, violations });
  }

  if (req.method === "GET" && url.pathname === "/api/admin/summary") {
    if (!requireAdmin(req, res)) return;
    const clients = readJson(paths.clients, []);
    const accounts = readJson(paths.accounts, []);
    const plugins = readJson(paths.plugins, []);
    const violations = readJson(paths.violations, []);
    const pluginCount = plugins.reduce((sum, item) => sum + (Array.isArray(item.rows) ? item.rows.length : 0), 0);
    const accountCount = accounts.reduce((sum, item) => sum + (Array.isArray(item.rows) ? item.rows.length : 0), 0);
    return jsonResponse(res, 200, {
      ok: true,
      totals: {
        clients: clients.length,
        accounts: accountCount,
        plugins: pluginCount,
        violations: violations.length
      },
      latest_clients: clients.slice(0, 10),
      latest_violations: violations.slice(0, 10)
    });
  }

  if (req.method === "GET" && url.pathname === "/api/admin/events") {
    if (!requireAdmin(req, res)) return;
    return jsonResponse(res, 200, { ok: true, rows: readJson(paths.events, []).slice(0, 200) });
  }

  if (req.method === "GET" && url.pathname === "/api/admin/plugins") {
    if (!requireAdmin(req, res)) return;
    return jsonResponse(res, 200, { ok: true, rows: readJson(paths.plugins, []) });
  }

  return jsonResponse(res, 404, { ok: false, error: "not found" });
}

function serveStatic(req, res, url) {
  const pathname = url.pathname === "/" ? "/index.html" : url.pathname;
  const filePath = path.normalize(path.join(publicDir, pathname));
  if (!filePath.startsWith(publicDir)) return textResponse(res, 403, "Forbidden");
  fs.readFile(filePath, (err, data) => {
    if (err) return textResponse(res, 404, "Not found");
    const ext = path.extname(filePath).toLowerCase();
    const types = {
      ".html": "text/html; charset=utf-8",
      ".css": "text/css; charset=utf-8",
      ".js": "application/javascript; charset=utf-8",
      ".svg": "image/svg+xml"
    };
    textResponse(res, 200, data, types[ext] || "application/octet-stream");
  });
}

ensureStore();

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);
  if (url.pathname.startsWith("/api/")) {
    routeApi(req, res, url).catch((error) => {
      jsonResponse(res, 500, { ok: false, error: error.message });
    });
    return;
  }
  serveStatic(req, res, url);
});

server.listen(port, () => {
  console.log(`YuexBot service listening on http://127.0.0.1:${port}`);
});
