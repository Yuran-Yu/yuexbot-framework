async function getJson(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

function text(value, fallback = "-") {
  return value === undefined || value === null || value === "" ? fallback : String(value);
}

function renderRows(target, rows, render, emptyText) {
  const el = document.getElementById(target);
  if (!rows || !rows.length) {
    el.innerHTML = `<div class="empty">${emptyText}</div>`;
    return;
  }
  el.innerHTML = rows.map(render).join("");
}

async function refresh() {
  const health = document.getElementById("health");
  try {
    const summary = await getJson("/api/admin/summary");
    const events = await getJson("/api/admin/events");

    health.textContent = "服务正常";
    health.classList.add("ok");
    document.getElementById("totalClients").textContent = summary.totals.clients;
    document.getElementById("totalAccounts").textContent = summary.totals.accounts;
    document.getElementById("totalPlugins").textContent = summary.totals.plugins;
    document.getElementById("totalViolations").textContent = summary.totals.violations;

    renderRows("clientRows", summary.latest_clients, (row) => `
      <div class="row">
        <div>
          <strong>${text(row.client_id)}</strong>
          <span>v${text(row.version)} / ${text(row.os)} / ${text(row.arch)}</span>
        </div>
        <span class="tag">${text(row.channel, "stable")}</span>
      </div>
    `, "暂无客户端上报");

    renderRows("violationRows", summary.latest_violations, (row) => `
      <div class="row">
        <div>
          <strong>${text(row.plugin && (row.plugin.name || row.plugin.id))}</strong>
          <span>${text(row.client_id)} / ${text(row.at)}</span>
        </div>
        <span class="tag danger">违规</span>
      </div>
    `, "暂无违规插件");

    renderRows("eventRows", events.rows, (row) => `
      <div class="event">
        <strong>${text(row.type)}</strong>
        <span>${text(row.client_id)}</span>
        <span>${text(row.at)} / count ${text(row.count, 1)}</span>
      </div>
    `, "暂无事件");
  } catch (error) {
    health.textContent = "服务异常";
    health.classList.remove("ok");
    console.error(error);
  }
}

document.getElementById("refreshBtn").addEventListener("click", refresh);
refresh();
setInterval(refresh, 15000);
