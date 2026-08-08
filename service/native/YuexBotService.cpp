// YuexBotService.cpp - native YuexBot service console
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../jade_dyn.h"
#include "../../../third_party/json.hpp"

using json = nlohmann::json;

static std::atomic<bool> g_running{true};
static const int kPort = 8787;
static std::string g_baseDir = ".";
static std::string g_dataDir = "service_data";
static uint32_t g_windowId = 0;

static const char* kServiceHtml = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>YuexBot Service</title>
<style>
:root{--bg:#f5f8fd;--card:#fff;--line:#e5edf8;--text:#172033;--muted:#748196;--primary:#4c8dff;--primary2:#2f6fe4;--soft:#eaf2ff;--soft2:#f8fbff;--ok:#19a463;--warn:#ff9900;--danger:#ff4d4f;--shadow:0 10px 28px rgba(40,86,150,.08)}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:"Microsoft YaHei UI","Segoe UI",Arial,sans-serif;font-size:14px;overflow:hidden}
.app{display:grid;grid-template-columns:236px minmax(0,1fr);height:100vh}.side{background:#fff;border-right:1px solid var(--line);padding:18px 14px;display:flex;flex-direction:column;gap:16px}.brand{display:flex;gap:12px;align-items:center;padding:6px 8px 12px;border-bottom:1px solid var(--line)}.brand svg{width:44px;height:44px;flex:0 0 44px}.brand b{display:block;font-size:18px;letter-spacing:.2px}.brand span{font-size:12px;color:var(--muted)}
.nav{display:grid;gap:7px}.nav button{height:42px;border:0;background:transparent;border-radius:8px;color:#526174;display:flex;align-items:center;gap:10px;padding:0 12px;text-align:left;font-weight:700;cursor:pointer}.nav button .ico{width:22px;text-align:center;color:#7b8ca6}.nav button.active,.nav button:hover{background:var(--soft);color:var(--primary2)}.nav button.active .ico{color:var(--primary2)}
.side-foot{margin-top:auto;border:1px solid var(--line);background:var(--soft2);border-radius:8px;padding:12px;color:var(--muted);font-size:12px;line-height:1.7}.main{min-width:0;height:100vh;overflow:auto;padding:22px 28px 34px}.top{display:flex;align-items:center;justify-content:space-between;gap:16px;margin-bottom:18px}.top h1{font-size:25px;margin:0 0 5px}.top p{margin:0;color:var(--muted)}.status{display:flex;gap:8px;align-items:center}.pill{height:34px;display:inline-flex;align-items:center;border-radius:999px;padding:0 14px;background:#effbf4;color:var(--ok);border:1px solid #bceccd;font-weight:800;white-space:nowrap}.pill.warn{background:#fff8ea;color:var(--warn);border-color:#ffe2a8}.btn{height:36px;border:0;border-radius:8px;background:var(--primary);color:#fff;padding:0 14px;font-weight:800;cursor:pointer}.btn.secondary{background:#eef4ff;color:var(--primary2);border:1px solid #d9e8ff}.cards{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-bottom:16px}.card{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:16px;box-shadow:var(--shadow);position:relative;overflow:hidden}.card:before{content:"";position:absolute;top:0;left:0;right:0;height:3px;background:var(--primary)}.card:nth-child(2):before{background:#23b6c8}.card:nth-child(3):before{background:#7c75ff}.card:nth-child(4):before{background:var(--danger)}.card span{color:var(--muted);font-weight:800}.card strong{display:block;font-size:32px;line-height:1.1;margin-top:9px}.card small{display:block;margin-top:8px;color:var(--muted)}.danger strong{color:var(--danger)}
.panel{background:#fff;border:1px solid var(--line);border-radius:8px;padding:16px;margin-bottom:16px;box-shadow:var(--shadow)}.panel-head{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-bottom:12px}.panel h2{font-size:18px;margin:0}.panel p{margin:4px 0 0;color:var(--muted)}.split{display:grid;grid-template-columns:1.05fr .95fr;gap:16px}.row{display:grid;grid-template-columns:minmax(0,1fr) auto;gap:12px;align-items:center;background:#fbfdff;border:1px solid var(--line);border-radius:8px;padding:11px 12px;margin-bottom:8px}.row b{display:block;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.row span,.event span,.table span{color:var(--muted);font-size:12px}.tag{display:inline-flex;align-items:center;height:24px;border-radius:999px;padding:0 10px;background:var(--soft);color:var(--primary2);font-weight:800;font-size:12px}.tag.ok{background:#edf9f2;color:var(--ok)}.tag.bad{background:#fff0ee;color:var(--danger)}.tag.warn{background:#fff8ea;color:var(--warn)}
.table{display:grid;gap:8px}.table-head,.table-row{display:grid;grid-template-columns:1.2fr .8fr .8fr .8fr;gap:10px;align-items:center}.table-head{color:var(--muted);font-size:12px;font-weight:800;padding:0 12px}.table-row{background:#fbfdff;border:1px solid var(--line);border-radius:8px;padding:11px 12px}.event{display:grid;grid-template-columns:110px 150px minmax(0,1fr) 88px;gap:10px;padding:10px 0;border-bottom:1px solid var(--line);align-items:center}.empty{padding:22px;text-align:center;color:var(--muted);border:1px dashed var(--line);border-radius:8px;background:#fbfdff}.page{display:none}.page.active{display:block}.code{font-family:Consolas,monospace;background:#f3f7ff;border:1px solid var(--line);border-radius:8px;padding:10px;overflow:auto}
@media(max-width:1100px){.cards{grid-template-columns:repeat(2,minmax(0,1fr))}.split{grid-template-columns:1fr}.event{grid-template-columns:100px minmax(0,1fr)}.event span:last-child{grid-column:1 / -1}}@media(max-width:820px){.app{grid-template-columns:1fr}.side{display:none}.main{padding:18px}.cards{grid-template-columns:1fr}.top{align-items:flex-start;flex-direction:column}.table-head{display:none}.table-row{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="app">
  <aside class="side">
    <div class="brand">
      <svg viewBox="0 0 256 256" fill="none"><defs><linearGradient id="aBg" x1="40" y1="28" x2="216" y2="228" gradientUnits="userSpaceOnUse"><stop stop-color="#77B6FF"/><stop offset=".55" stop-color="#4C8DFF"/><stop offset="1" stop-color="#2D66D6"/></linearGradient><linearGradient id="aWave" x1="62" y1="160" x2="191" y2="110" gradientUnits="userSpaceOnUse"><stop stop-color="#CFF6FF"/><stop offset="1" stop-color="#FFFFFF"/></linearGradient></defs><rect x="28" y="24" width="200" height="200" rx="48" fill="url(#aBg)"/><path d="M159 60c-31 0-57 26-57 58 0 33 26 59 59 59 11 0 20-3 29-8-11 19-31 32-55 32-40 0-72-32-72-72 0-39 31-70 69-71 9 0 18 1 27 2z" fill="#FDFEFF"/><path d="M69 163c18-15 37-22 56-22 25 0 44 8 63 28" stroke="url(#aWave)" stroke-width="14" stroke-linecap="round"/><circle cx="86" cy="151" r="6" fill="#fff"/><circle cx="126" cy="140" r="6" fill="#fff"/><circle cx="172" cy="163" r="6" fill="#fff"/></svg>
      <div><b>YuexBot</b><span>服务端控制台</span></div>
    </div>
    <nav class="nav">
      <button class="active" data-page="overview"><span class="ico">⌂</span>服务概览</button>
      <button data-page="updates"><span class="ico">↥</span>更新管理</button>
      <button data-page="users"><span class="ico">◇</span>用户统计</button>
      <button data-page="plugins"><span class="ico">▣</span>插件风控</button>
      <button data-page="accounts"><span class="ico">◎</span>账号数据</button>
    </nav>
    <div class="side-foot">本程序独立运行，不影响 YuexBot 框架启动、OneBot 连接和插件加载。</div>
  </aside>
  <main class="main">
    <header class="top">
      <div><h1>YuexBot 服务端</h1><p>更新检查、数据上报、账号统计、插件违规检测</p></div>
      <div class="status"><button class="btn secondary" onclick="refresh()">刷新</button><div class="pill warn" id="health">检查中</div></div>
    </header>
    <section class="cards">
      <article class="card"><span>客户端</span><strong id="clients">0</strong><small>已上报框架实例</small></article>
      <article class="card"><span>账号</span><strong id="accounts">0</strong><small>已统计登录账号</small></article>
      <article class="card"><span>插件</span><strong id="plugins">0</strong><small>已上报插件数量</small></article>
      <article class="card danger"><span>违规</span><strong id="violations">0</strong><small>命中风控策略</small></article>
    </section>
    <section class="page active" id="page-overview">
      <section class="split"><article class="panel"><div class="panel-head"><div><h2>最近客户端</h2><p>框架启动或定时上报后显示在这里</p></div></div><div id="clientRows"></div></article><article class="panel"><div class="panel-head"><div><h2>插件风险</h2><p>发现高危权限或违规关键词会标记</p></div></div><div id="violationRows"></div></article></section>
      <section class="panel"><div class="panel-head"><div><h2>上报事件</h2><p>服务端接收到的框架、账号、插件数据流水</p></div></div><div id="eventRows"></div></section>
    </section>
    <section class="page" id="page-updates">
      <section class="panel"><div class="panel-head"><div><h2>更新策略</h2><p>框架通过 /api/v1/update/check 检查新版本</p></div><span class="tag ok">Stable</span></div><div class="row"><div><b>YuexBot 1.1.0</b><span>内置 UI、原生插件 SDK 基线、多账号连接优化</span></div><span class="tag">当前正式版</span></div><div class="code">POST http://127.0.0.1:8787/api/v1/update/check</div></section>
    </section>
    <section class="page" id="page-users">
      <section class="panel"><div class="panel-head"><div><h2>客户端统计</h2><p>按安装实例聚合，不保存明文 QQ 号</p></div></div><div id="userRows"></div></section>
    </section>
    <section class="page" id="page-plugins">
      <section class="panel"><div class="panel-head"><div><h2>插件合规检测</h2><p>服务端接收插件清单并返回风险结果</p></div><span class="tag">自动检测</span></div><div id="pluginRiskRows"></div></section>
    </section>
    <section class="page" id="page-accounts">
      <section class="panel"><div class="panel-head"><div><h2>账号数据</h2><p>每个框架实例上报的账号状态、群数、好友数</p></div></div><div id="accountRows"></div></section>
    </section>
  </main>
</div>
<script>
const API_BASE="http://127.0.0.1:8787";
const api=(u,o)=>fetch(API_BASE+u,o).then(r=>r.json());
const esc=v=>String(v??"-").replace(/[&<>"']/g,m=>({"&":"&amp;","<":"&lt;",">":"&gt;","\"":"&quot;","'":"&#39;"}[m]));
function list(id,rows,fn,empty){document.getElementById(id).innerHTML=rows&&rows.length?rows.map(fn).join(""):`<div class="empty">${empty}</div>`}
function shortId(v){const s=String(v||"-");return s.length>16?s.slice(0,8)+"..."+s.slice(-6):s}
document.querySelectorAll(".nav button").forEach(btn=>btn.addEventListener("click",()=>{
  document.querySelectorAll(".nav button").forEach(b=>b.classList.remove("active"));
  document.querySelectorAll(".page").forEach(p=>p.classList.remove("active"));
  btn.classList.add("active");
  document.getElementById("page-"+btn.dataset.page).classList.add("active");
}));
async function refresh(){
  try{
    const health=await api("/api/health"); const summary=await api("/api/admin/summary"); const events=await api("/api/admin/events");
    const healthEl=document.getElementById("health"); healthEl.textContent=health.ok?"服务运行中":"服务异常"; healthEl.className=health.ok?"pill":"pill warn";
    clients.textContent=summary.totals.clients; accounts.textContent=summary.totals.accounts; plugins.textContent=summary.totals.plugins; violations.textContent=summary.totals.violations;
    list("clientRows",summary.latest_clients,r=>`<div class="row"><div><b>${esc(shortId(r.client_id))}</b><span>v${esc(r.version)} / ${esc(r.os)} / ${esc(r.arch)} / ${esc(r.last_seen)}</span></div><span class="tag">${esc(r.channel||"stable")}</span></div>`,"暂无客户端上报");
    list("violationRows",summary.latest_violations,r=>`<div class="row"><div><b>${esc(r.plugin?.name||r.plugin?.id||"未知插件")}</b><span>${esc((r.plugin?.hits||r.hits||[]).join(", ")||r.at)}</span></div><span class="tag bad">需处理</span></div>`,"暂无违规插件");
    list("eventRows",events.rows,r=>`<div class="event"><b>${esc(r.type)}</b><span>${esc(shortId(r.client_id))}</span><span>${esc(r.at)}</span><span class="tag">${esc(r.count||1)}</span></div>`,"暂无上报事件");
    list("userRows",summary.latest_clients,r=>`<div class="row"><div><b>${esc(shortId(r.client_id))}</b><span>账号 ${esc(r.accounts)} / 插件 ${esc(r.plugins)} / 消息 ${esc(r.messages)} / 事件 ${esc(r.events)}</span></div><span class="tag ok">${esc(r.version)}</span></div>`,"暂无用户统计");
    list("pluginRiskRows",summary.latest_violations,r=>`<div class="row"><div><b>${esc(r.plugin?.name||r.plugin?.id||"未知插件")}</b><span>${esc((r.plugin?.hits||[]).join("，")||"命中风险策略")}</span></div><span class="tag bad">阻断建议</span></div>`,"暂无插件风险记录");
    list("accountRows",summary.account_reports||[],r=>`<div class="panel"><div class="panel-head"><div><h2>${esc(shortId(r.client_id))}</h2><p>${esc(r.reported_at)}</p></div></div><div class="table"><div class="table-head"><b>账号</b><b>连接</b><b>群</b><b>好友</b></div>${(r.rows||[]).map(a=>`<div class="table-row"><b>${esc(a.display_name||a.account_hash)}</b><span>${esc(a.mode)} / ${esc(a.status)}</span><span>${esc(a.group_count)}</span><span>${esc(a.friend_count)}</span></div>`).join("")}</div></div>`,"暂无账号数据");
  }catch(e){const h=document.getElementById("health"); h.textContent="服务异常"; h.className="pill warn"; console.error(e)}
}
refresh(); setInterval(refresh,15000);
</script>
</body>
</html>
)HTML";

static std::string now_iso() {
    char buf[64] = {};
    std::time_t t = std::time(nullptr);
    std::tm tmv = {};
    gmtime_s(&tmv, &t);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return buf;
}

static std::string read_file(const std::string& path, const std::string& fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fallback;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void write_file(const std::string& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

static json read_json(const std::string& name, const json& fallback) {
    try { return json::parse(read_file(g_dataDir + "\\" + name, fallback.dump())); }
    catch (...) { return fallback; }
}

static void write_json(const std::string& name, const json& data) {
    write_file(g_dataDir + "\\" + name, data.dump(2));
}

static void append_json(const std::string& name, const json& item) {
    json rows = read_json(name, json::array());
    rows.insert(rows.begin(), item);
    if (rows.size() > 5000) rows.erase(rows.begin() + 5000, rows.end());
    write_json(name, rows);
}

static void upsert_json(const std::string& name, const std::string& key, const json& item) {
    json rows = read_json(name, json::array());
    bool found = false;
    for (auto& row : rows) {
        if (row.value(key, "") == item.value(key, "")) {
            row.update(item);
            found = true;
            break;
        }
    }
    if (!found) rows.insert(rows.begin(), item);
    write_json(name, rows);
}

static std::string json_text(const json& obj, const std::string& key, const std::string& fallback = "") {
    if (!obj.contains(key) || obj[key].is_null()) return fallback;
    const json& v = obj[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) return std::to_string(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return fallback;
}

static int json_int(const json& obj, const std::string& key, int fallback = 0) {
    if (!obj.contains(key) || obj[key].is_null()) return fallback;
    try {
        if (obj[key].is_number_integer() || obj[key].is_number_unsigned()) return obj[key].get<int>();
        if (obj[key].is_string()) return std::stoi(obj[key].get<std::string>());
    } catch (...) {}
    return fallback;
}

static std::string tiny_hash(const std::string& text) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    char buf[32] = {};
    snprintf(buf, sizeof(buf), "%016llx", h);
    return buf;
}

static std::string client_id_from(const json& body) {
    std::string seed = json_text(body, "install_id") + "|" + json_text(body, "machine_id") + "|" + json_text(body, "version");
    if (seed == "||") seed = std::to_string(GetTickCount64());
    return tiny_hash(seed);
}

static std::string http_json(int code, const json& body) {
    std::string data = body.dump();
    std::ostringstream res;
    res << "HTTP/1.1 " << code << " OK\r\n";
    res << "Content-Type: application/json; charset=utf-8\r\n";
    res << "Access-Control-Allow-Origin: *\r\n";
    res << "Access-Control-Allow-Methods: GET,POST,OPTIONS\r\n";
    res << "Access-Control-Allow-Headers: content-type,authorization,x-yuexbot-token\r\n";
    res << "Cache-Control: no-store\r\n";
    res << "Content-Length: " << data.size() << "\r\n\r\n";
    res << data;
    return res.str();
}

static std::string http_html(const std::string& html) {
    std::ostringstream res;
    res << "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: " << html.size() << "\r\n\r\n" << html;
    return res.str();
}

static bool contains_lower(std::string text, std::string needle) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return !needle.empty() && text.find(needle) != std::string::npos;
}

static json analyze_plugin(const json& plugin) {
    std::string id = json_text(plugin, "id", json_text(plugin, "plugin_id"));
    std::string name = json_text(plugin, "name");
    std::string desc = json_text(plugin, "description");
    std::string hay = id + "\n" + name + "\n" + desc;
    json hits = json::array();
    if (contains_lower(hay, "remote shell")) hits.push_back("keyword:remote shell");
    if (contains_lower(hay, "token stealer")) hits.push_back("keyword:token stealer");
    if (plugin.contains("permissions") && plugin["permissions"].is_array()) {
        for (const auto& perm : plugin["permissions"]) {
            std::string p = perm.is_string() ? perm.get<std::string>() : perm.dump();
            if (p == "process_exec" || p == "filesystem_full" || p == "network_raw") hits.push_back("permission:" + p);
        }
    }
    return json{{"status", hits.empty() ? "clean" : "blocked"}, {"hits", hits}};
}

static json summary_payload() {
    json clients = read_json("clients.json", json::array());
    json accounts = read_json("accounts.json", json::array());
    json plugins = read_json("plugins.json", json::array());
    json violations = read_json("violations.json", json::array());
    int accountCount = 0, pluginCount = 0;
    for (auto& row : accounts) if (row.contains("rows") && row["rows"].is_array()) accountCount += (int)row["rows"].size();
    for (auto& row : plugins) if (row.contains("rows") && row["rows"].is_array()) pluginCount += (int)row["rows"].size();
    return json{
        {"ok", true},
        {"totals", {{"clients", clients.size()}, {"accounts", accountCount}, {"plugins", pluginCount}, {"violations", violations.size()}}},
        {"latest_clients", clients},
        {"latest_violations", violations},
        {"account_reports", accounts},
        {"plugin_reports", plugins}
    };
}

static std::string handle_api(const std::string& method, const std::string& path, const std::string& bodyText) {
    if (method == "OPTIONS") return http_json(204, json::object());
    json body = json::object();
    if (!bodyText.empty()) {
        try { body = json::parse(bodyText); }
        catch (...) { return http_json(400, {{"ok", false}, {"error", "invalid json"}}); }
    }
    if (method == "GET" && path == "/api/health") {
        return http_json(200, {{"ok", true}, {"service", "yuexbot-native-service"}, {"version", "0.1.0"}, {"time", now_iso()}});
    }
    if (method == "POST" && path == "/api/v1/update/check") {
        std::string current = json_text(body, "version", "0.0.0");
        bool update = current < "1.1.0";
        return http_json(200, {{"ok", true}, {"update_available", update}, {"current_version", current}, {"latest", {{"version", "1.1.0"}, {"url", "https://example.com/yuexbot/YuexBot-v1.1.0.zip"}, {"sha256", ""}, {"mandatory", false}, {"notes", json::array({"内置 UI 正式包", "原生插件 SDK 基线", "多账号连接优化"})}}}});
    }
    if (method == "POST" && path == "/api/v1/telemetry/framework") {
        std::string cid = client_id_from(body);
        json item = {{"client_id", cid}, {"version", json_text(body, "framework_version", json_text(body, "version"))}, {"channel", json_text(body, "channel", "stable")}, {"os", json_text(body, "os")}, {"arch", json_text(body, "arch")}, {"accounts", json_int(body, "accounts")}, {"plugins", json_int(body, "plugins")}, {"messages", json_int(body, "messages")}, {"events", json_int(body, "events")}, {"last_seen", now_iso()}};
        upsert_json("clients.json", "client_id", item);
        append_json("events.json", {{"type", "framework"}, {"client_id", cid}, {"at", now_iso()}});
        return http_json(200, {{"ok", true}, {"client_id", cid}});
    }
    if (method == "POST" && path == "/api/v1/telemetry/accounts") {
        std::string cid = client_id_from(body);
        json rows = json::array();
        if (body.contains("accounts") && body["accounts"].is_array()) {
            for (auto& a : body["accounts"]) {
                std::string qq = json_text(a, "qq", json_text(a, "self_id"));
                rows.push_back({{"account_hash", tiny_hash(qq)}, {"display_name", json_text(a, "display_name", json_text(a, "nickname"))}, {"mode", json_text(a, "mode")}, {"status", json_text(a, "status")}, {"group_count", json_int(a, "group_count")}, {"friend_count", json_int(a, "friend_count")}});
            }
        }
        upsert_json("accounts.json", "client_id", {{"client_id", cid}, {"rows", rows}, {"reported_at", now_iso()}});
        append_json("events.json", {{"type", "accounts"}, {"client_id", cid}, {"count", rows.size()}, {"at", now_iso()}});
        return http_json(200, {{"ok", true}, {"count", rows.size()}});
    }
    if (method == "POST" && path == "/api/v1/telemetry/plugins") {
        std::string cid = client_id_from(body);
        json rows = json::array(), violations = json::array();
        if (body.contains("plugins") && body["plugins"].is_array()) {
            for (auto plugin : body["plugins"]) {
                json result = analyze_plugin(plugin);
                plugin["compliance"] = result["status"];
                plugin["hits"] = result["hits"];
                rows.push_back(plugin);
                if (result["status"] != "clean") {
                    json v = {{"client_id", cid}, {"plugin", plugin}, {"at", now_iso()}};
                    violations.push_back(v);
                    append_json("violations.json", v);
                }
            }
        }
        upsert_json("plugins.json", "client_id", {{"client_id", cid}, {"rows", rows}, {"reported_at", now_iso()}});
        append_json("events.json", {{"type", "plugins"}, {"client_id", cid}, {"count", rows.size()}, {"violations", violations.size()}, {"at", now_iso()}});
        return http_json(200, {{"ok", true}, {"count", rows.size()}, {"violations", violations}});
    }
    if (method == "GET" && path == "/api/admin/summary") return http_json(200, summary_payload());
    if (method == "GET" && path == "/api/admin/events") return http_json(200, {{"ok", true}, {"rows", read_json("events.json", json::array())}});
    return http_json(404, {{"ok", false}, {"error", "not found"}});
}

static std::string strip_query(std::string target) {
    size_t hash = target.find('#');
    if (hash != std::string::npos) target.resize(hash);
    size_t query = target.find('?');
    if (query != std::string::npos) target.resize(query);
    return target;
}

static void send_all(SOCKET client, const std::string& response) {
    const char* data = response.c_str();
    int remaining = (int)response.size();
    while (remaining > 0) {
        int sent = send(client, data, remaining, 0);
        if (sent <= 0) break;
        data += sent;
        remaining -= sent;
    }
}

static void handle_client(SOCKET client) {
    char buffer[65536] = {};
    int received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) { closesocket(client); return; }
    std::string req(buffer, received);
    std::istringstream first(req.substr(0, req.find("\r\n")));
    std::string method, target;
    first >> method >> target;
    target = strip_query(target);
    size_t bodyPos = req.find("\r\n\r\n");
    std::string body = bodyPos == std::string::npos ? "" : req.substr(bodyPos + 4);
    std::string response;
    if (target == "/" || target == "/index.html") {
        response = http_html(kServiceHtml);
    } else if (target.rfind("/api/", 0) == 0) {
        response = handle_api(method, target, body);
    } else {
        response = http_json(404, {{"ok", false}, {"error", "not found"}});
    }
    send_all(client, response);
    closesocket(client);
}

static void http_server_thread() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    SOCKET server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == INVALID_SOCKET) { WSACleanup(); return; }
    BOOL reuse = TRUE;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kPort);
    if (bind(server, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(server);
        WSACleanup();
        return;
    }
    listen(server, SOMAXCONN);
    while (g_running.load()) {
        SOCKET client = accept(server, nullptr, nullptr);
        if (client != INVALID_SOCKET) std::thread(handle_client, client).detach();
    }
    closesocket(server);
    WSACleanup();
}

static bool write_service_ui(std::string& outDir) {
    outDir = g_baseDir + "\\YuexBotService_ui";
    CreateDirectoryA(outDir.c_str(), NULL);
    write_file(outDir + "\\index.html", kServiceHtml);
    return true;
}

static const char* on_all_closed(uint32_t, const char*) {
    g_running.store(false);
    if (cleanup_all_windows) cleanup_all_windows();
    ExitProcess(0);
    return "";
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exe(exePath);
    size_t slash = exe.find_last_of("\\/");
    if (slash != std::string::npos) {
        g_baseDir = exe.substr(0, slash);
        SetCurrentDirectoryA(g_baseDir.c_str());
    }
    g_dataDir = g_baseDir + "\\service_data";
    CreateDirectoryA(g_dataDir.c_str(), NULL);

    std::thread(http_server_thread).detach();

    if (!load_jade_dll()) {
        MessageBoxA(NULL, "Failed to load JadeView_x64.dll", "YuexBot Service", MB_ICONERROR);
        return 1;
    }
    jade_on("window-all-closed", on_all_closed);
    int initOk = 0;
    #if defined(_WIN64)
    initOk = JadeView_init(0, NULL, NULL, "YuexBot Service", "com.yuexbot.service", 0);
    #else
    initOk = JadeView_init(0, NULL, NULL);
    #endif
    if (!initOk) return 1;

    std::string uiDir;
    write_service_ui(uiDir);
    char url[1024] = {};
    if (set_protocol_service_path && set_protocol_service_path(uiDir.c_str(), url, sizeof(url)) == 1) {
    } else {
        std::string file = uiDir + "\\index.html";
        std::replace(file.begin(), file.end(), '\\', '/');
        snprintf(url, sizeof(url), "file:///%s", file.c_str());
    }

    WebViewWindowOptions opts = {};
    opts.title = "YuexBot Service";
    opts.width = 1280;
    opts.height = 820;
    opts.resizable = 1;
    opts.frame_style = "title-overlay";
    opts.theme = "Light";
    opts.min_width = 960;
    opts.min_height = 640;
    opts.focus = 1;
    WebViewSettings settings = {};
    settings.background_throttling = 0;

    #if defined(_WIN64)
    g_windowId = create_webview_window(url, 0, &opts, &settings);
    #else
    g_windowId = create_webview_window(url, 0, nullptr, nullptr);
    #endif
    if (!g_windowId) {
        MessageBoxA(NULL, "Failed to create service window", "YuexBot Service", MB_ICONERROR);
        return 1;
    }
    run_message_loop();
    g_running.store(false);
    unload_jade_dll();
    return 0;
}
