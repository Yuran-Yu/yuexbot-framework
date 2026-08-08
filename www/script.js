<script>
try {
(function(){
"use strict";

/* ===== State ===== */
var state = {
  connected: false, logs: [], friends: [], groups: [],
  currentFilter: "all", currentPage: "dashboard",
  connMode: "reverse-ws", startTime: Date.now(),
  autoScroll: true, modalCb: null,
  plugins: { },
  accounts: [], editingAccount: null, ctxTarget: null,
  friendLoaded: false, groupLoaded: false
};

function el(id) { return document.getElementById(id); }
function esc(t) {
  if (t === undefined || t === null) return "";
  var s = String(t);
  s = s.replace(/&/g,"&amp;"); s = s.replace(/</g,"&lt;");
  s = s.replace(/>/g,"&gt;"); s = s.replace(/"/g,"&quot;");
  return s;
}
function pad(n) { return n < 10 ? "0" + n : "" + n; }

/* ===== IPC ===== */
function ipc(cmd, data) {
  return new Promise(function(resolve) {
    if (typeof jade === "undefined") {
      console.log("[IPC] jade not available: " + cmd);
      resolve(null); return;
    }
    try {
      var result = jade.invoke(cmd, data || {});
      if (result && typeof result.then === "function") {
        result.then(function(r) { resolve(r); }).catch(function(e) { resolve(null); });
      } else { resolve(result); }
    } catch (e) { resolve(null); }
  });
}

/* ===== Toast ===== */
function showToast(msg, type) {
  var t = el("toast"); if (!t) return;
  t.textContent = msg;
  t.className = "toast " + (type || "info") + " show";
  setTimeout(function(){ t.classList.remove("show"); }, 3000);
}

/* ===== Modal ===== */
function showModal(title, html, confirmText, onConfirm) {
  el("modalTitle").textContent = title;
  el("modalContent").innerHTML = html;
  el("modalConfirm").textContent = confirmText || "确认";
  state.modalCb = onConfirm;
  el("modalOverlay").classList.add("show");
  setTimeout(function(){
    var inp = document.querySelector(".modal-input,.modal-textarea");
    if (inp) inp.focus();
  }, 100);
}
function closeModal() { el("modalOverlay").classList.remove("show"); state.modalCb = null; }
function confirmModal() { if (state.modalCb) state.modalCb(); closeModal(); }

/* ===== Navigation ===== */
function navigateTo(page) {
  state.currentPage = page;
  var items = document.querySelectorAll(".sidebar-item");
  for (var i = 0; i < items.length; i++) {
    var p = items[i].getAttribute("data-page");
    if (p === page) items[i].classList.add("active"); else items[i].classList.remove("active");
  }
  var pages = document.querySelectorAll(".page");
  for (var j = 0; j < pages.length; j++) {
    if (pages[j].id === "page-" + page) pages[j].classList.add("active"); else pages[j].classList.remove("active");
  }
  if (page === "groups" && !state.groupLoaded) { state.groupLoaded = true; loadGroups(); }
  if (page === "plugins") loadPlugins();
  if (page === "accounts") loadAccounts();
  if (page === "dashboard") updateConnInfo();
}

/* ===== Status UI ===== */
function updateStatusUI(c) {
  state.connected = c;
  var pill = el("topStatus"); var text = el("topStatusText");
  var dot = el("statusDot"); var connText = el("statusConnText");
  if (c) {
    pill.className = "pill connected"; text.textContent = "已连接";
    dot.style.background = "var(--green)";
    connText.textContent = "已连接"; connText.style.color = "var(--green)";
    // Update buttons
    var btnStart = el("btnConnect");
    if (btnStart) { btnStart.textContent = "断开连接"; btnStart.className = "action-btn danger"; }
  } else {
    pill.className = "pill disconnected"; text.textContent = "未连接";
    dot.style.background = "var(--orange)";
    connText.textContent = "未连接"; connText.style.color = "var(--orange)";
    var btnStart2 = el("btnConnect");
    if (btnStart2) { btnStart2.textContent = "启动Bot"; btnStart2.className = "action-btn primary"; }
  }
}

/* ===== Connection ===== */
function toggleConnection() {
  if (state.connected) {
    showModal("断开连接", "<p>确定要断开与 OneBot 的连接吗？</p>", "断开", function(){
      ipc("disconnect"); showToast("正在断开连接...", "info");
    });
  } else { doConnect(); }
}
function doConnect() {
  var host = el("setHost") ? el("setHost").value : "127.0.0.1";
  var port = parseInt((el("setPort") ? el("setPort").value : "3001") || "3001");
  var token = el("setToken") ? el("setToken").value : "";
  ipc("connect", {host:host, port:port, token:token});
  showToast("正在连接 " + host + ":" + port + "...", "info");
}
function doReconnect() {
  if (state.connected) ipc("disconnect");
  setTimeout(function(){ doConnect(); }, 500);
}

/* ===== Logs ===== */
function appendLog(d) {
  if (!d) return;
  if (!d.message && d.content) d.message = d.content;
  if (!d.content && d.message) d.content = d.message;
  state.logs.push(d);
  renderLogs("logBody", "logSearch", "all");
  renderLogs("logBodyMsg", "logSearchMsg", "msg");
  renderLogs("logBodyAll", "logSearchAll", "all");
  var ev = el("statusEvents"); if (ev) ev.textContent = state.logs.length;
  var bf = el("badgeMsg"); if (bf) bf.textContent = state.logs.length;
}
function renderLogs(bodyId, searchId, scope) {
  var body = el(bodyId); if (!body) return;
  var searchEl = el(searchId);
  var search = searchEl ? searchEl.value.toLowerCase() : "";
  var filtered = state.logs.filter(function(l) {
    var searchText = (l.message || l.content || ''); if (search && searchText.toLowerCase().indexOf(search) === -1) return false;
    if (scope === "msg" && l.type !== "group" && l.type !== "private" && l.type !== "群聊" && l.type !== "私聊") return false;
    if (state.currentFilter && state.currentFilter !== "all" && l.type !== state.currentFilter) return false;
    return true;
  });
  var html = "";
  for (var i = 0; i < filtered.length; i++) {
    var l = filtered[i];
    var time = l.time || "--:--:--";
    var tagCls = "sys", tagText = "系统";
    if (l.type === "group" || l.type === "群聊") { tagCls = "group"; tagText = "群聊"; }
    else if (l.type === "private" || l.type === "私聊") { tagCls = "private"; tagText = "私聊"; }
    else if (l.type === "warning" || l.type === "警告") { tagCls = "warn"; tagText = "警告"; }
    else if (l.type === "error" || l.type === "错误") { tagCls = "error"; tagText = "错误"; }
    else if (l.type === "notice" || l.type === "通知") { tagCls = "sys"; tagText = "通知"; }
    else if (l.type === "系统") { tagCls = "sys"; tagText = "系统"; }
    html += '<div class="log-line"><span class="time">' + esc(time) + '</span><span class="tag ' + tagCls + '">' + tagText + '</span><span class="msg">' + esc(l.message || l.content || '') + '</span></div>';
  }
  if (!html) html = '<div class="empty-state"><p>暂无日志</p><p class="hint">启动Bot后日志将在此显示</p></div>';
  body.innerHTML = html;
  if (state.autoScroll) body.scrollTop = body.scrollHeight;
}

/* ===== Plugins ===== */
function loadPlugins() {
  ipc("get-status").then(function(s) {
    if (s && s.plugins) state.plugins = s.plugins;
    renderPlugins();
  }).catch(function(){});
}
/* ===== Accounts ===== */
function loadAccounts() {
  ipc("get-accounts").then(function(r) {
    if (r && Array.isArray(r)) { state.accounts = r; }
    renderAccounts();
  }).catch(function(){ renderAccounts(); });
}
function renderAccounts() {
  var grid = el("accountGrid"); if (!grid) return;
  if (!state.accounts || state.accounts.length === 0) {
    grid.innerHTML = "<div class='empty-state'><p>暂无账号</p><p class='hint'>点击上方按钮添加第一个账号</p></div>";
    return;
  }
  var colors = ["blue","purple","green","orange"];
  var modeNames = {"reverse-ws":"反向 WebSocket","forward-ws":"正向 WebSocket","http-post":"HTTP POST"};
  var html = "";
  for (var i = 0; i < state.accounts.length; i++) {
    var a = state.accounts[i];
    var color = colors[i % colors.length];
    var initial = (a.name || "").charAt(0).toUpperCase();
    var statusClass = a.connected ? "online" : (a.connecting ? "connecting" : "offline");
    var statusText = a.connected ? "已连接" : (a.connecting ? "连接中" : "未连接");
    var modeLabel = modeNames[a.mode] || a.mode || "反向 WebSocket";
    var btnHtml;
    if (a.connected) {
      btnHtml = '<button class="action-btn danger" onclick="event.stopPropagation();disconnectAccount(' + i + ')">断开</button>';
    } else {
      btnHtml = '<button class="action-btn primary" onclick="event.stopPropagation();connectAccount(' + i + ')">连接</button>';
    }
    btnHtml += '<button class="action-btn" onclick="event.stopPropagation();editAccount(' + i + ')">编辑</button>';
    btnHtml += '<button class="action-btn danger" onclick="event.stopPropagation();deleteAccount(' + i + ')">删除</button>';
    html += '<div class="account-card" data-index="' + i + '" oncontextmenu="showAccountCtx(event,' + i + ')">';
    html += '<div class="account-card-header">';
    html += '<div class="account-avatar ' + color + '">' + esc(initial) + '</div>';
    html += '<div class="account-card-info">';
    html += '<div class="account-name">' + esc(a.name || "") + '</div>';
    html += '<div class="account-qq">QQ: ' + esc(a.qq || "---") + '</div>';
    html += '</div></div>';
    html += '<div class="account-status ' + statusClass + '"><span class="dot"></span>' + statusText + '</div>';
    html += '<div class="account-meta">';
    html += '<div class="account-meta-item">' + esc(modeLabel) + '</div>';
    html += '<div class="account-meta-item">' + esc(a.host || "127.0.0.1") + ':' + esc(a.port || "3001") + '</div>';
    html += '</div>';
    html += '<div class="account-actions">' + btnHtml + '</div>';
    html += '</div>';
  }
  grid.innerHTML = html;
}
function showAccountCtx(e, idx) {
  e.preventDefault();
  state.ctxTarget = idx;
  var ctx = el("contextMenu");
  ctx.style.left = e.clientX + "px";
  ctx.style.top = e.clientY + "px";
  ctx.classList.add("show");
}
function connectAccount(idx) {
  var a = state.accounts[idx]; if (!a) return;
  ipc("connect-account", {index:idx, host:a.host||"127.0.0.1", port:parseInt(a.port||3001), token:a.token||"", mode:a.mode||"reverse-ws"});
   showToast("正在连接 " + (a.name||a.qq||"") + "...", "info");
}
function disconnectAccount(idx) {
  ipc("disconnect-account", {index:idx});
  showToast("正在断开连接...", "info");
}
function editAccount(idx) {
  var a = state.accounts[idx]; if (!a) return;
  state.editingAccount = idx;
  el("cfgName").value = a.name || "";
  el("cfgQQ").value = a.qq || "";
  el("cfgMode").value = a.mode || "reverse-ws";
  el("cfgHost").value = a.host || "127.0.0.1";
  el("cfgPort").value = a.port || "3001";
  el("cfgToken").value = a.token || "";
  el("accountModalTitle").textContent = "编辑账号";
  updateCfgHint();
  el("accountModalOverlay").classList.add("show");
}
function deleteAccount(idx) {
  showModal("删除账号", "<p>"+"确定要删除此账号配置吗？"+"</p>", "删除账号", function(){
    ipc("delete-account", {index:idx});
    showToast("账号已删除", "info");
    setTimeout(loadAccounts, 300);
  });
}
function openAddAccount() {
  state.editingAccount = null;
  el("cfgName").value = "";
  el("cfgQQ").value = "";
  el("cfgMode").value = "reverse-ws";
  el("cfgHost").value = "127.0.0.1";
  el("cfgPort").value = "3001";
  el("cfgToken").value = "";
  el("accountModalTitle").textContent = "添加账号";
  updateCfgHint();
  el("accountModalOverlay").classList.add("show");
}
function updateCfgHint() {
  var mode = el("cfgMode").value;
  var hints = {"reverse-ws": "反向 WebSocket 模式下，OneBot 服务端会主动连接本程序", "forward-ws": "正向 WebSocket 模式下，本程序会主动连接 OneBot 服务端", "http-post": "HTTP POST 模式下，事件通过 HTTP 回调推送"};
  el("cfgHint").textContent = hints[mode] || "";
}

function renderPlugins() {
  var grid = el("pluginGrid"); if (!grid) return;
  var icons = {"auto-reply":"💬","logger":"📝","group-admin":"🛡️","translate":"🌐","scheduler":"⏰","music":"🎵","image":"🖼️","rss":"📰"};
  var names = {"auto-reply":"自动回复","logger":"日志记录","group-admin":"群管助手","translate":"翻译助手","scheduler":"定时任务","music":"音乐播放","image":"图片处理","rss":"RSS订阅"};
  var descs = {"auto-reply":"收到消息时自动发送预设回复","logger":"记录所有群消息和事件","group-admin":"管理群成员、踢人、禁言","translate":"自动翻译群消息","scheduler":"定时发送消息","music":"搜索并发送音乐","image":"图片处理与水印","rss":"RSS源消息推送"};
  var html = "";
  for (var k in state.plugins) {
    var on = state.plugins[k];
    var bg = on ? "var(--primary-bg)" : "#F5F5F5";
    html += '<div class="plugin-card"><div class="plugin-icon" style="background:' + bg + '">' + (icons[k]||"🔌") + '</div><div class="plugin-info"><div class="plugin-name">' + esc(names[k]||k) + '</div><div class="plugin-desc">' + esc(descs[k]||"") + '</div><div class="plugin-meta"><span class="' + (on?"enabled":"disabled") + '">' + (on?"已启用":"已禁用") + '</span></div></div><div class="plugin-controls"><label class="toggle"><input type="checkbox" data-plugin="' + k + '"' + (on?' checked':'') + '><span class="slider"></span></label></div></div>';
  }
  grid.innerHTML = html;
  // Bind toggle events
  var toggles = grid.querySelectorAll('input[data-plugin]');
  for (var i = 0; i < toggles.length; i++) {
    toggles[i].addEventListener("change", function(){
      var pk = this.getAttribute("data-plugin");
      state.plugins[pk] = this.checked;
      renderPlugins();
      ipc("save-settings", {plugins: state.plugins});
      showToast((this.checked ? "启用" : "禁用") + "插件: " + (names[pk]||pk), "info");
    });
  }
}

/* ===== Groups ===== */
function loadGroups() {
  ipc("get-groups").then(function(g) {
    if (g && Array.isArray(g)) state.groups = g;
    renderGroups();
  }).catch(function(){});
}
function renderGroups() {
  var wrap = el("groupsTableBody");
  if (!wrap) { wrap = el("groupsTable"); if (!wrap) return; }
  var html = "";
  for (var i = 0; i < state.groups.length; i++) {
    var g = state.groups[i];
    html += '<tr><td>' + (g.group_id || g.id || '-') + '</td><td>' + esc(g.group_name || g.name || '-') + '</td><td>' + (g.member_count || g.count || 0) + '</td><td><button class="table-btn" onclick="window._sendGroupMsg(' + (g.group_id||g.id) + ',\'' + esc(g.group_name||g.name||'') + '\')">发送消息</button></td></tr>';
  }
  if (!html) html = '<tr><td colspan="4" class="empty-state"><p>暂无群数据</p><p class="hint">连接后群列表将在此显示</p></td></tr>';
  wrap.innerHTML = html; var emptyEl = el("groupsEmpty"); if(emptyEl) emptyEl.style.display = state.groups.length ? "none" : "block"; if(wrap.tagName === "TBODY"){}else{wrap.innerHTML = html;}
}
window._sendGroupMsg = function(gid, gname) {
  showModal("发送组聊消息", '<p style="margin-bottom:12px">发送到: <b>' + esc(gname) + '</b> (' + gid + ')</p><textarea class="modal-textarea" id="msgContent" placeholder="输入消息内容..."></textarea>', "发送", function(){
    var c = el("msgContent").value;
    if (c && c.trim()) { ipc("send-message", {mode:1, target_id:gid, message:c.trim()}); showToast("消息已发送到 " + gname, "success"); }
    else { showToast("消息不能为空", "error"); }
  });
};

/* ===== Friends ===== */
function loadFriends() {
  ipc("get-friends").then(function(f) {
    if (f && Array.isArray(f)) {
      state.friends = f;
      var sf = el("statFriends"); if (sf) sf.textContent = f.length;
    }
  }).catch(function(){});
}
function loadSettings() {
    updateConnInfo();
  ipc("load-settings").then(function(s) {
    if (!s) return;
    if (s.host && el("setHost")) el("setHost").value = s.host;
    if (s.port && el("setPort")) el("setPort").value = s.port;
    if (s.token && el("setToken")) el("setToken").value = s.token;
    if (s.connMode) {
      state.connMode = s.connMode;
      var radios = document.querySelectorAll("#connModeGroup .radio-btn");
      for (var i = 0; i < radios.length; i++) {
        if (radios[i].getAttribute("data-mode") === s.connMode) radios[i].classList.add("active");
        else radios[i].classList.remove("active");
      }
    }
    if (s.autoStart && el("setAutoStart")) el("setAutoStart").checked = s.autoStart;
    if (s.tray !== undefined && el("setTray")) el("setTray").checked = s.tray;
    if (s.logSave !== undefined && el("setLogSave")) el("setLogSave").checked = s.logSave;
    if (s.plugins) state.plugins = s.plugins;
  });
}
function saveSettings() {
  var s = {
    host: el("setHost") ? el("setHost").value : "127.0.0.1",
    port: el("setPort") ? el("setPort").value : "3001",
    token: el("setToken") ? el("setToken").value : "",
    connMode: state.connMode,
    autoStart: el("setAutoStart") ? el("setAutoStart").checked : false,
    tray: el("setTray") ? el("setTray").checked : true,
    logSave: el("setLogSave") ? el("setLogSave").checked : true,
    plugins: state.plugins
  };
  ipc("save-settings", s);
  showToast("设置已保存", "success");
  updateConnInfo();
}

/* ===== Clock ===== */

/* ===== Connection Info Display ===== */
function updateConnInfo() {
  ipc("load-settings").then(function(s) {
    var mode = (s && s.connMode) ? s.connMode : state.connMode;
    var host = (s && s.host) ? s.host : "127.0.0.1";
    var port = (s && s.port) ? s.port : "3001";
    var token = (s && s.token) ? s.token : "";
    var modeNames = {"reverse-ws":"反向 WebSocket","forward-ws":"正向 WebSocket","http-post":"HTTP POST"};
    var modeLabels = {"reverse-ws":"被动连接","forward-ws":"主动连接","http-post":"回调推送"};
    var modeEl = el("connMode"); if (modeEl) modeEl.textContent = modeNames[mode] || mode;
    var rItem = el("connReverseItem");
    var fItem = el("connForwardItem");
    var hItem = el("connHttpItem");
    if (rItem) { rItem.className = mode === "reverse-ws" ? "conn-port-item active" : "conn-port-item dimmed"; }
    if (fItem) { fItem.className = mode === "forward-ws" ? "conn-port-item active" : "conn-port-item dimmed"; }
    if (hItem) { hItem.className = mode === "http-post" ? "conn-port-item active" : "conn-port-item dimmed"; }
    var rp = el("connReversePort");
    var fp = el("connForwardPort");
    var hp = el("connHttpPort");
    if (mode === "reverse-ws") {
      if (rp) rp.textContent = port;
      if (fp) fp.textContent = "3002";
      if (hp) hp.textContent = "3003";
    } else if (mode === "forward-ws") {
      if (rp) rp.textContent = "3001";
      if (fp) fp.textContent = port;
      if (hp) hp.textContent = "3003";
    } else {
      if (rp) rp.textContent = "3001";
      if (fp) fp.textContent = "3002";
      if (hp) hp.textContent = port;
    }
    var apiEl = el("connApiAddr"); if (apiEl) apiEl.textContent = host;
    var tokEl = el("connTokenStatus");
    if (tokEl) {
      if (token && token.length > 0) {
        tokEl.textContent = "已设置";
        tokEl.className = "conn-detail-value online";
      } else {
        tokEl.textContent = "未设置";
        tokEl.className = "conn-detail-value unset";
      }
    }
  });
}
function updateClock() {
  var now = new Date();
  var str = pad(now.getHours()) + ":" + pad(now.getMinutes()) + ":" + pad(now.getSeconds());
  var elTime = el("statusTime"); if (elTime) elTime.textContent = str;
  var elapsed = Math.floor((Date.now() - state.startTime) / 1000);
  var h = Math.floor(elapsed / 3600);
  var m = Math.floor((elapsed % 3600) / 60);
  var s = elapsed % 60;
  var upEl = el("statusUptime"); if (upEl) upEl.textContent = pad(h) + ":" + pad(m) + ":" + pad(s);
}

/* ===== Init ===== */
function initApp() {
  try {
    // Sidebar navigation
    var navItems = document.querySelectorAll(".sidebar-item");
    for (var i = 0; i < navItems.length; i++) {
      navItems[i].addEventListener("click", function(){
        var page = this.getAttribute("data-page");
        if (page) navigateTo(page);
      });
    }

    // Quick action buttons
    var btnStart = el("btnConnect");
    if (btnStart) btnStart.addEventListener("click", toggleConnection);
    var btnReconnect = el("btnReconnect");
    if (btnReconnect) btnReconnect.addEventListener("click", doReconnect);
    var btnClearLog = el("btnClearLogs");
    if (btnClearLog) btnClearLog.addEventListener("click", function(){
      state.logs = [];
      renderLogs("logBody", "logSearch", "all");
      renderLogs("logBodyMsg", "logSearchMsg", "msg");
      renderLogs("logBodyAll", "logSearchAll", "all");
      ipc("clear-logs");
      showToast("日志已清空", "info");
    });
    var btnExport = el("btnExportLogs");
    if (btnExport) btnExport.addEventListener("click", function(){
      ipc("export-logs").then(function(r) {
        if (r && r.path) showToast("日志已导出: " + r.path, "success");
        else showToast("导出失败", "error");
      }).catch(function(){});
    });

    var btnClearLogs2 = el("btnClearLogs2");
    if (btnClearLogs2) btnClearLogs2.addEventListener("click", function(){
      state.logs = [];
      renderLogs("logBody", "logSearch", "all");
      renderLogs("logBodyMsg", "logSearchMsg", "msg");
      renderLogs("logBodyAll", "logSearchAll", "all");
      ipc("clear-logs");
      showToast("日志已清空", "info");
    });
    // Log search
    var logSearch = el("logSearch");
    if (logSearch) logSearch.addEventListener("input", function(){ renderLogs("logBody", "logSearch", "all"); });
    var logSearchMsg = el("logSearchMsg");
    if (logSearchMsg) logSearchMsg.addEventListener("input", function(){ renderLogs("logBodyMsg", "logSearchMsg", "msg"); });
    var logSearchAll = el("logSearchAll");
    if (logSearchAll) logSearchAll.addEventListener("input", function(){ renderLogs("logBodyAll", "logSearchAll", "all"); });

    // Log clear buttons (on log tabs)
    var btnClearLogs3 = el("btnClearLogs3");
    if (btnClearLogs3) btnClearLogs3.addEventListener("click", function(){
      state.logs = [];
      renderLogs("logBody", "logSearch", "all");
      renderLogs("logBodyMsg", "logSearchMsg", "msg");
      renderLogs("logBodyAll", "logSearchAll", "all");
      ipc("clear-logs");
      showToast("日志已清空", "info");
    });
    var btnClearLogsAll = el("btnClearLogsAll");
    if (btnClearLogsAll) btnClearLogsAll.addEventListener("click", function(){
      state.logs = [];
      renderLogs("logBody", "logSearch", "all");
      renderLogs("logBodyMsg", "logSearchMsg", "msg");
      renderLogs("logBodyAll", "logSearchAll", "all");
      ipc("clear-logs");
      showToast("日志已清空", "info");
    });

    // Modal buttons
    el("modalCancel").addEventListener("click", closeModal);
    el("modalConfirm").addEventListener("click", confirmModal);
    el("modalOverlay").addEventListener("click", function(e){ if (e.target === this) closeModal(); });

    // Account modal
    el("accountModalCancel").addEventListener("click", function(){ el("accountModalOverlay").classList.remove("show"); });
    el("accountModalConfirm").addEventListener("click", function(){
      var data = {
        name: el("cfgName").value,
        qq: el("cfgQQ").value,
        mode: el("cfgMode").value,
        host: el("cfgHost").value,
        port: el("cfgPort").value,
        token: el("cfgToken").value
      };
      if (state.editingAccount !== null) data.index = state.editingAccount;
      ipc("save-account", data).then(function(r) {
        if (r && r.success) {
          showToast("账号已保存", "success");
          el("accountModalOverlay").classList.remove("show");
          state.editingAccount = null;
          if (state.currentPage === "accounts") loadAccounts();
        } else {
          showToast("保存失败", "error");
        }
      }).catch(function(){});
    });
    el("accountModalOverlay").addEventListener("click", function(e){ if (e.target === this) this.classList.remove("show"); });

    // Connection mode radios
    var radioBtns = document.querySelectorAll("#connModeGroup .radio-btn");
    for (var r = 0; r < radioBtns.length; r++) {
      radioBtns[r].addEventListener("click", function(){
        var mode = this.getAttribute("data-mode");
        state.connMode = mode;
        var all = document.querySelectorAll("#connModeGroup .radio-btn");
        for (var j = 0; j < all.length; j++) all[j].classList.remove("active");
        this.classList.add("active");
        var evtRow = el("wsEventRow");
        if (evtRow) {
          if (mode === "forward-ws" || mode === "http-post") evtRow.style.display = "flex";
          else evtRow.style.display = "none";
        }
      });
    }

    // Settings save/test
    var btnSave = el("btnSaveSettings");
    if (btnSave) btnSave.addEventListener("click", saveSettings);
    var btnTest = el("btnTestConn");
    if (btnTest) btnTest.addEventListener("click", function(){
      showToast("正在测试连接...", "info");
      ipc("connect", {
        host: el("setHost") ? el("setHost").value : "127.0.0.1",
        port: parseInt((el("setPort") ? el("setPort").value : "3001") || "3001"),
        token: el("setToken") ? el("setToken").value : ""
      });
    });

    
    // Add account button
    var btnAdd = el("btnAddAccount");
    if (btnAdd) btnAdd.addEventListener("click", openAddAccount);

    // Config mode change hint
    var cfgModeEl = el("cfgMode");
    if (cfgModeEl) cfgModeEl.addEventListener("change", updateCfgHint);

    // Context menu
    var ctxMenu = el("contextMenu");
    document.addEventListener("click", function(e) {
      if (!ctxMenu.contains(e.target)) ctxMenu.classList.remove("show");
    });
    var ctxItems = ctxMenu.querySelectorAll(".context-menu-item");
    for (var c = 0; c < ctxItems.length; c++) {
      ctxItems[c].addEventListener("click", function(){
        var action = this.getAttribute("data-action");
        if (action === "ctx-connect") { connectAccount(state.ctxTarget); }
        else if (action === "ctx-disconnect") { ipc("disconnect"); showToast("正在断开...", "info"); }
        else if (action === "ctx-edit") { editAccount(state.ctxTarget); }
        else if (action === "ctx-delete") {
          showModal("删除账号", "<p>确定要删除此账号配置吗？</p>", "删除", function(){
            ipc("delete-account", {index: state.ctxTarget});
            showToast("账号已删除", "info");
          });
        }
        ctxMenu.classList.remove("show");
      });
    }

    // Theme toggle (placeholder)
    var btnTheme = el("btnThemeToggle");
    if (btnTheme) btnTheme.addEventListener("click", function(){
      var isDark = document.body.classList.toggle("dark-theme");
      showToast(isDark ? "已切换到深色模式" : "已切换到浅色模式", "info");
    });

    // Quit button
    var btnQuit = el("btnQuit");
    if (btnQuit) btnQuit.addEventListener("click", function(){
      showModal("退出", "<p>确定要退出 YuexBot 吗？</p>", "退出", function(){
        ipc("quit");
        showToast("正在退出...", "info");
      });
    });

    // Group search
    var grpSearch = el("groupSearch");
    if (grpSearch) grpSearch.addEventListener("input", function(){
      var v = this.value.toLowerCase();
      var rows = document.querySelectorAll("#groupsTableBody tr");
      for (var i = 0; i < rows.length; i++) {
        var text = rows[i].textContent.toLowerCase();
        rows[i].style.display = text.indexOf(v) > -1 ? "" : "none";
      }
    });

    // Clock
    setInterval(updateClock, 1000); updateClock();

    // Load settings and initial data
    loadSettings();
    loadAccounts();
    loadFriends();
        ipc("get-status").then(function(s) {
      if (s) {
        updateStatusUI(!!s.connected);
        if (s.connected) {
          var sub = el("titleSubtitle");
          if (sub) sub.textContent = "QQ:" + (s.qq||"---") + " · OneBot 11";
        }
        if (s.friends !== undefined) { var sf = el("statFriends"); if (sf) sf.textContent = s.friends; }
        if (s.groups !== undefined) { var sg = el("statGroups"); if (sg) sg.textContent = s.groups; }
        if (s.messages !== undefined) { var sm = el("statMessages"); if (sm) sm.textContent = s.messages; }
      }
    });
    ipc("get-logs").then(function(l) {
      if (l && Array.isArray(l)) {
        state.logs = l;
        renderLogs("logBody", "logSearch", "all");
        renderLogs("logBodyMsg", "logSearchMsg", "msg");
        renderLogs("logBodyAll", "logSearchAll", "all");
      }
    }).catch(function(){});

    // JadeView event handlers
    if (typeof jade !== "undefined") {
      try {
        jade.on("status-changed", function(d) {
          updateStatusUI(!!d.connected);
          if (d.connected) {
            var sub = el("titleSubtitle");
            if (sub) sub.textContent = "QQ:" + (d.qq||"") + " · " + (d.name||"");
            showToast("已连接 QQ " + (d.qq||"") + " (" + (d.name||"") + ")", "success");
          } else {
            showToast(d.error || "连接已断开", "error");
          updateConnInfo();
          }
        });
        jade.on("new-log", function(d) { appendLog(d); });
        jade.on("system-stats", function(d) {
          var cpu = el("pillCPU"); if (cpu) cpu.textContent = d.cpu + "%";
          var mem = el("pillMem"); if (mem) mem.textContent = d.memory + "MB";
          var lat = el("pillLatency"); if (lat) lat.textContent = d.latency + "ms";
          var scpu = el("statCPU"); if (scpu) scpu.textContent = d.cpu + "%";
          var smem = el("statMem"); if (smem) smem.textContent = d.memory + "MB";
          var slat = el("statLatency"); if (slat) slat.textContent = d.latency + "ms";
          if (d.friends !== undefined) { var sf = el("statFriends"); if (sf) sf.textContent = d.friends; }
          if (d.groups !== undefined) { var sg = el("statGroups"); if (sg) sg.textContent = d.groups; }
          if (d.messages !== undefined) { var sm = el("statMessages"); if (sm) sm.textContent = d.messages; var stmsg = el("statusMsg"); if (stmsg) stmsg.textContent = d.messages; }
          var ev = el("statusEvents"); if (ev) ev.textContent = d.events || 0;
        });
        jade.on("logs-cleared", function() {
          state.logs = [];
          renderLogs("logBody", "logSearch", "all");
          renderLogs("logBodyMsg", "logSearchMsg", "msg");
          renderLogs("logBodyAll", "logSearchAll", "all");
        });
      } catch(e) { console.error("[YuexBot] Jade event error:", e); }
    }

    // Render initial plugins
    loadPlugins();

    // Log tab filtering
    var logTabBtns = document.querySelectorAll(".log-tab");
    for (var t = 0; t < logTabBtns.length; t++) {
      logTabBtns[t].addEventListener("click", function(){
        var filter = this.getAttribute("data-filter");
        var siblings = this.parentNode.querySelectorAll(".log-tab");
        for (var s = 0; s < siblings.length; s++) siblings[s].classList.remove("active");
        this.classList.add("active");
        state.currentFilter = filter || "all";
        var scope = this.getAttribute("data-scope") || "all";
        if (scope === "msg") renderLogs("logBodyMsg", "logSearchMsg", "msg");
        else if (scope === "alllogs") renderLogs("logBodyAll", "logSearchAll", "all");
        else renderLogs("logBody", "logSearch", "all");
      });
    }

    console.log("[YuexBot] initApp complete!");
  } catch(e) { console.error("[YuexBot] initApp error:", e); }
}

if (document.readyState === "loading") {
  document.addEventListener("DOMContentLoaded", initApp);
} else {
  initApp();
}

})();
} catch(err) {
  console.error("[YuexBot] FATAL:", err);
  document.body.insertAdjacentHTML("afterbegin", '<div style="padding:20px;background:#ffe0e0;color:red;font-size:14px;position:fixed;top:0;left:0;right:0;z-index:9999"><b>JS Error:</b> ' + err.message + '</div>');
}
</script>