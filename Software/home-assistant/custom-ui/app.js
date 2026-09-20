const $ = (id) => document.getElementById(id);

const ENTITY = {
  linkState: "sensor.industrial_controller_link_state",
  linkAge: "sensor.industrial_controller_link_age",
  reconnects: "sensor.industrial_controller_reconnects",
  timeouts: "sensor.industrial_controller_timeouts",
  rtd: "sensor.industrial_controller_rtd_temperature",
  supply24: "sensor.industrial_controller_supply_24v",
  supply5: "sensor.industrial_controller_supply_5v",
  di: "sensor.industrial_controller_di_bitmap",
  relay: "sensor.industrial_controller_relay_bitmap",
  fault: "sensor.industrial_controller_fault_bitmap",
  wdg: "sensor.industrial_controller_watchdog",
  alive: "sensor.industrial_controller_alive_bitmap",
  gateway: "binary_sensor.industrial_controller_gateway_online",
  faultActive: "binary_sensor.industrial_controller_fault_active",
  relayMask: "number.industrial_controller_relay_mask",
  allOn: "button.industrial_controller_all_relays_on",
  allOff: "button.industrial_controller_all_relays_off",
  pulse: "button.industrial_controller_pulse_relay_1",
  clear: "button.industrial_controller_clear_faults",
  save: "button.industrial_controller_save_config",
  load: "button.industrial_controller_load_config",
};

const state = {
  mode: "login",
  tokens: null,
  latest: {},
  chart: null,
  demoTimer: null,
  demoStep: 0,
};

function refreshIcons() {
  if (window.lucide) window.lucide.createIcons();
}

function setVisible(element, visible) {
  element.classList.toggle("app-hidden", !visible);
}

function showLogin(message = "") {
  state.mode = "login";
  setVisible($("loginScreen"), true);
  setVisible($("app"), false);
  $("loginError").textContent = message;
  setStatus("等待连接");
  refreshIcons();
}

function showApp(mode) {
  state.mode = mode;
  setVisible($("loginScreen"), false);
  setVisible($("app"), true);
  setVisible($("demoBadge"), mode === "demo");
  $("commandMode").textContent = mode === "demo" ? "演示控制" : "HA 控制";
  $("commandMode").className = mode === "demo" ? "badge bg-orange-lt" : "badge bg-green-lt";
  refreshIcons();
}

function setStatus(text, className = "") {
  $("connectionText").textContent = text;
  $("statusDot").className = `status-dot ${className}`.trim();
}

function stateClass(value) {
  const text = String(value || "").toUpperCase();
  if (text === "ONLINE" || text === "ON") return "online";
  if (text === "DEGRADED") return "degraded";
  if (text === "OFFLINE" || text === "OFF") return "offline";
  return "";
}

function entityValue(entityId, fallback = "--") {
  if (state.mode === "demo") return state.latest[entityId] ?? fallback;
  return state.latest[entityId]?.state ?? fallback;
}

function numberValue(entityId, fallback = 0) {
  const value = Number(entityValue(entityId, NaN));
  return Number.isFinite(value) ? value : fallback;
}

function formatInteger(value) {
  const number = Number(value);
  return Number.isFinite(number) ? new Intl.NumberFormat("zh-CN").format(number) : String(value ?? "--");
}

function hex(value, width) {
  const number = Number(value);
  return Number.isFinite(number) ? `0x${number.toString(16).toUpperCase().padStart(width, "0")}` : "--";
}

function createChart() {
  if (state.chart) return;
  const colors = ["#206bc4", "#2fb344", "#f59f00", "#d63939", "#ae3ec9", "#0ca678", "#f76707", "#4c6ef5"];
  state.chart = new Chart($("aiChart"), {
    type: "line",
    data: {
      labels: [],
      datasets: Array.from({ length: 8 }, (_, index) => ({
        label: `AI${index}`,
        data: [],
        borderColor: colors[index],
        backgroundColor: colors[index],
        borderWidth: 2,
        pointRadius: 0,
        tension: 0.28,
        spanGaps: true,
      })),
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: { duration: 180 },
      interaction: { mode: "index", intersect: false },
      plugins: {
        legend: { position: "bottom", labels: { boxWidth: 10, boxHeight: 10, usePointStyle: true } },
      },
      scales: {
        x: { grid: { display: false }, ticks: { maxTicksLimit: 8, color: "#667382" } },
        y: { beginAtZero: false, grid: { color: "rgba(102,115,130,.14)" }, ticks: { color: "#667382" } },
      },
    },
  });
}

function appendChartPoint() {
  createChart();
  state.chart.data.labels.push(new Date().toLocaleTimeString("zh-CN", { hour12: false }));
  for (let index = 0; index < 8; index += 1) {
    state.chart.data.datasets[index].data.push(numberValue(`sensor.industrial_controller_ai_${index}`, null));
  }
  while (state.chart.data.labels.length > 40) {
    state.chart.data.labels.shift();
    state.chart.data.datasets.forEach((dataset) => dataset.data.shift());
  }
  state.chart.update();
  $("sampleCount").textContent = `${state.chart.data.labels.length} samples`;
}

function renderAiGrid() {
  $("aiGrid").innerHTML = Array.from({ length: 8 }, (_, index) => {
    const id = `sensor.industrial_controller_ai_${index}`;
    return `
      <div class="col-6">
        <div class="io-tile">
          <div class="label">AI${index}</div>
          <div class="value">${formatInteger(entityValue(id))}</div>
        </div>
      </div>`;
  }).join("");
}

function renderBits(element, value, count) {
  const number = Number(value);
  element.innerHTML = Array.from({ length: count }, (_, index) => {
    const active = Number.isFinite(number) && ((number >> index) & 1) === 1;
    return `<span class="bit ${active ? "active" : ""}">${index}</span>`;
  }).join("");
}

function renderTelemetryTable() {
  const rows = [
    [ENTITY.linkState, "链路状态"],
    [ENTITY.linkAge, "链路延迟"],
    [ENTITY.rtd, "RTD 温度"],
    [ENTITY.supply24, "24V 电源"],
    [ENTITY.supply5, "5V 电源"],
    [ENTITY.di, "DI 位图"],
    [ENTITY.relay, "继电器位图"],
    [ENTITY.fault, "故障位图"],
    [ENTITY.wdg, "看门狗刷新"],
  ].map(([entity, label]) => `
    <tr>
      <td><code>${label}</code></td>
      <td>${entityValue(entity)}</td>
      <td>${new Date().toLocaleTimeString("zh-CN", { hour12: false })}</td>
    </tr>`);
  $("telemetryTable").innerHTML = rows.join("");
}

function render() {
  const linkState = String(entityValue(ENTITY.linkState, "--"));
  $("linkState").textContent = linkState;
  $("linkState").className = `metric-value mt-3 ${stateClass(linkState)}`.trim();
  $("linkMeta").textContent = `age=${formatInteger(entityValue(ENTITY.linkAge))}ms · reconnects=${formatInteger(entityValue(ENTITY.reconnects))}`;

  $("ai0Value").textContent = formatInteger(entityValue("sensor.industrial_controller_ai_0"));
  $("aiMeta").textContent = `AI1 ${formatInteger(entityValue("sensor.industrial_controller_ai_1"))} · AI2 ${formatInteger(entityValue("sensor.industrial_controller_ai_2"))} · AI3 ${formatInteger(entityValue("sensor.industrial_controller_ai_3"))}`;

  const rtd = Number(entityValue(ENTITY.rtd, NaN));
  $("rtdValue").textContent = Number.isFinite(rtd) ? `${rtd.toFixed(2)} °C` : "--";

  const supply24 = Number(entityValue(ENTITY.supply24, NaN));
  const supply5 = Number(entityValue(ENTITY.supply5, NaN));
  $("supplyValue").textContent = Number.isFinite(supply24) && Number.isFinite(supply5)
    ? `${supply24.toFixed(2)} / ${supply5.toFixed(2)} V`
    : "--";
  $("supplyMeta").textContent = `24V ${Number.isFinite(supply24) ? supply24.toFixed(2) : "--"}V · 5V ${Number.isFinite(supply5) ? supply5.toFixed(2) : "--"}V`;

  $("faultValue").textContent = hex(entityValue(ENTITY.fault), 4);
  $("watchdogValue").textContent = formatInteger(entityValue(ENTITY.wdg));
  $("linkAge").textContent = `${formatInteger(entityValue(ENTITY.linkAge))} ms`;
  $("reconnects").textContent = formatInteger(entityValue(ENTITY.reconnects));
  $("timeouts").textContent = formatInteger(entityValue(ENTITY.timeouts));
  $("alive").textContent = hex(entityValue(ENTITY.alive), 4);
  $("lastUpdate").textContent = `最后更新 ${new Date().toLocaleTimeString("zh-CN", { hour12: false })}`;

  const gateway = entityValue(ENTITY.gateway) === "on";
  setStatus(
    state.mode === "demo" ? "演示数据运行中" : gateway ? "已连接 Home Assistant" : "网关离线",
    gateway ? "online" : "offline"
  );

  renderAiGrid();
  renderBits($("diBits"), entityValue(ENTITY.di), 8);
  renderBits($("relayBits"), entityValue(ENTITY.relay), 8);
  renderTelemetryTable();
  appendChartPoint();
  refreshIcons();
}

async function haApi(path, options = {}) {
  const response = await fetch(path, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(state.tokens?.access_token ? { Authorization: `Bearer ${state.tokens.access_token}` } : {}),
      ...(options.headers || {}),
    },
  });
  if (response.status === 401) {
    state.tokens = null;
    localStorage.removeItem("haTokens");
    showLogin("登录已过期，请重新登录。");
    throw new Error("unauthorized");
  }
  if (!response.ok) {
    const text = await response.text();
    throw new Error(text || `${response.status} ${response.statusText}`);
  }
  return response.status === 204 ? null : response.json();
}

async function fetchStates() {
  const states = await haApi("/api/states");
  state.latest = Object.fromEntries(states.map((entity) => [entity.entity_id, entity]));
  render();
}

async function login(event) {
  event.preventDefault();
  $("loginError").textContent = "";
  setStatus("登录中", "degraded");
  const username = $("username").value.trim();
  const password = $("password").value;
  try {
    const base = location.origin;
    const clientId = `${base}/`;
    const redirectUri = `${base}/?auth_callback=1`;
    const flow = await fetch("/auth/login_flow", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ client_id: clientId, handler: ["homeassistant", null], redirect_uri: redirectUri }),
    }).then((response) => response.json());
    const result = await fetch(`/auth/login_flow/${flow.flow_id}`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ client_id: clientId, username, password }),
    }).then((response) => response.json());
    if (result.type !== "create_entry") throw new Error("账号或密码错误");
    const tokenResponse = await fetch("/auth/token", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: new URLSearchParams({
        grant_type: "authorization_code",
        code: result.result,
        client_id: clientId,
      }),
    });
    if (!tokenResponse.ok) throw new Error("无法获取访问令牌");
    const tokens = await tokenResponse.json();
    state.tokens = {
      ...tokens,
      hassUrl: base,
      clientId,
      expires: Date.now() + Number(tokens.expires_in) * 1000,
    };
    localStorage.setItem("haTokens", JSON.stringify(state.tokens));
    showApp("real");
    await fetchStates();
  } catch (error) {
    $("loginError").textContent = `登录失败：${error.message}`;
    setStatus("登录失败", "offline");
  }
}

async function pressButton(entityId) {
  await haApi("/api/services/button/press", {
    method: "POST",
    body: JSON.stringify({ entity_id: entityId }),
  });
}

async function sendCommand(button) {
  const command = button.dataset.command;
  const result = $("commandResult");
  result.className = "alert alert-info mb-0";
  result.textContent = `正在执行 ${command}...`;
  try {
    if (state.mode === "demo") {
      await new Promise((resolve) => setTimeout(resolve, 350));
      result.className = "alert alert-success mb-0";
      result.textContent = `演示命令 ${command} 已执行。`;
      return;
    }
    if (command === "relay") {
      await pressButton(button.dataset.mask === "0" ? ENTITY.allOff : ENTITY.allOn);
    } else if (command === "pulse") {
      await pressButton(ENTITY.pulse);
    } else if (command === "clear") {
      await pressButton(ENTITY.clear);
    } else if (command === "save") {
      await pressButton(ENTITY.save);
    } else if (command === "load") {
      await pressButton(ENTITY.load);
    }
    result.className = "alert alert-success mb-0";
    result.textContent = `命令 ${command} 已发送，Home Assistant 正在执行。`;
    await fetchStates();
  } catch (error) {
    result.className = "alert alert-danger mb-0";
    result.textContent = `执行失败：${error.message}`;
  }
}

function demoSnapshot() {
  state.demoStep += 1;
  const t = state.demoStep;
  for (let index = 0; index < 8; index += 1) {
    state.latest[`sensor.industrial_controller_ai_${index}`] = Math.round(1720 + index * 95 + Math.sin(t / 3 + index) * 64 + (t % 7) * 3);
  }
  state.latest[ENTITY.linkState] = t % 13 === 0 ? "DEGRADED" : "ONLINE";
  state.latest[ENTITY.linkAge] = 38 + (t % 9);
  state.latest[ENTITY.reconnects] = 3;
  state.latest[ENTITY.timeouts] = 1;
  state.latest[ENTITY.rtd] = (24500 + Math.sin(t / 5) * 420) / 1000;
  state.latest[ENTITY.supply24] = 24.12 + Math.sin(t / 4) * 0.04;
  state.latest[ENTITY.supply5] = 5.016 + Math.cos(t / 5) * 0.01;
  state.latest[ENTITY.di] = 0b10110101;
  state.latest[ENTITY.relay] = 0b00101101;
  state.latest[ENTITY.fault] = 0;
  state.latest[ENTITY.wdg] = 1430 + t;
  state.latest[ENTITY.alive] = 0x001f;
  state.latest[ENTITY.gateway] = "on";
  render();
}

function startDemo() {
  stopDemo();
  showApp("demo");
  demoSnapshot();
  state.demoTimer = setInterval(demoSnapshot, 1200);
}

function stopDemo() {
  if (state.demoTimer) clearInterval(state.demoTimer);
  state.demoTimer = null;
  if (state.chart) {
    state.chart.destroy();
    state.chart = null;
  }
}

async function restoreSession() {
  const raw = localStorage.getItem("haTokens");
  if (!raw) {
    setStatus("等待连接");
    return;
  }
  try {
    state.tokens = JSON.parse(raw);
    showApp("real");
    await fetchStates();
  } catch {
    localStorage.removeItem("haTokens");
    state.tokens = null;
    showLogin("会话已失效，请重新登录。");
  }
}

$("loginForm").addEventListener("submit", login);
$("demoLogin").addEventListener("click", startDemo);
$("logoutButton").addEventListener("click", () => {
  localStorage.removeItem("haTokens");
  state.tokens = null;
  stopDemo();
  showLogin();
});
$("refreshButton").addEventListener("click", () => {
  if (state.mode === "demo") demoSnapshot();
  else fetchStates().catch((error) => {
    $("errorAlert").textContent = `刷新失败：${error.message}`;
    setVisible($("errorAlert"), true);
  });
});
document.querySelectorAll(".command-button").forEach((button) => {
  button.addEventListener("click", () => sendCommand(button));
});

createChart();
refreshIcons();
restoreSession();
setInterval(() => {
  if (state.mode === "real") fetchStates().catch(() => {});
}, 2000);
