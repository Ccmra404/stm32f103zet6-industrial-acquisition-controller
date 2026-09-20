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
  lastAck: "sensor.industrial_controller_last_ack",
  busFrame: "sensor.industrial_controller_last_bus_frame",
};

const COMMAND_ID = {
  relay: 0x0001,
  pulse: 0x0002,
  clear: 0x0004,
  save: 0x0200,
  load: 0x0201,
};

const RESULT_TEXT = {
  0: "成功",
  1: "指令不受支持",
  2: "参数无效",
  3: "设备忙",
  4: "设备未就绪",
  5: "超出范围",
  6: "存储错误",
  7: "安全锁生效",
};

const ACK_POLL_INTERVAL_MS = 250;
const ACK_TIMEOUT_MS = 6000;
const STATE_TIMEOUT_MS = 4000;
const STALE_AFTER_MS = 15000;
const UNAVAILABLE_STATES = new Set(["", "unavailable", "unknown", "none", "null"]);

const state = {
  mode: "login",
  tokens: null,
  latest: {},
  chart: null,
  demoTimer: null,
  demoStep: 0,
  commandPending: false,
  pollErrors: 0,
};

let demoRelayMask = 0b00101101;

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
  $("commandResult").textContent = mode === "demo"
    ? "演示模式：命令只在本地模拟，不会下发到设备。"
    : "命令下发后等待 STM32 应答，失败时显示具体原因。";
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

function entityRecord(entityId) {
  return state.latest[entityId] ?? null;
}

function isUnavailable(entityId) {
  if (state.mode === "demo") return state.latest[entityId] === undefined;
  const record = entityRecord(entityId);
  if (!record) return true;
  return UNAVAILABLE_STATES.has(String(record.state ?? "").trim().toLowerCase());
}

function entityValue(entityId, fallback = "--") {
  if (state.mode === "demo") return state.latest[entityId] ?? fallback;
  if (isUnavailable(entityId)) return fallback;
  return entityRecord(entityId).state;
}

function numberValue(entityId, fallback = 0) {
  const value = Number(entityValue(entityId, NaN));
  return Number.isFinite(value) ? value : fallback;
}

function formatMeasurement(entityId, digits, unit = "") {
  const value = Number(entityValue(entityId, NaN));
  return Number.isFinite(value) ? `${value.toFixed(digits)}${unit}` : "--";
}

function entityUpdatedAt(entityId) {
  const record = entityRecord(entityId);
  if (!record?.last_updated) return null;
  const timestamp = new Date(record.last_updated);
  return Number.isNaN(timestamp.getTime()) ? null : timestamp;
}

function isStale(entityId, limitMs = STALE_AFTER_MS) {
  if (state.mode === "demo") return false;
  const updatedAt = entityUpdatedAt(entityId);
  if (!updatedAt) return true;
  return Date.now() - updatedAt.getTime() > limitMs;
}

function formatClock(timestamp) {
  return timestamp ? timestamp.toLocaleTimeString("zh-CN", { hour12: false }) : "--";
}

function formatBitField(value, width) {
  const number = Number(value);
  if (!Number.isFinite(number)) return "--";
  return `${hex(number, width)} · ${number}`;
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
  if (state.chart) return state.chart;
  if (typeof Chart === "undefined") {
    const hint = $("chartEmpty");
    hint.textContent = "图表组件加载失败，请检查网络后刷新";
    setVisible(hint, true);
    return null;
  }
  $("chartEmpty").textContent = "暂无采样数据";
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
  return state.chart;
}

function appendChartPoint() {
  const chart = createChart();
  if (!chart) return;
  const values = [];
  for (let index = 0; index < 8; index += 1) {
    values.push(numberValue(`sensor.industrial_controller_ai_${index}`, null));
  }
  const hasSample = values.some((value) => Number.isFinite(value));
  setVisible($("chartEmpty"), !hasSample && state.chart.data.labels.length === 0);
  if (!hasSample) return;

  state.chart.data.labels.push(new Date().toLocaleTimeString("zh-CN", { hour12: false }));
  for (let index = 0; index < 8; index += 1) {
    state.chart.data.datasets[index].data.push(values[index]);
  }
  while (state.chart.data.labels.length > 40) {
    state.chart.data.labels.shift();
    state.chart.data.datasets.forEach((dataset) => dataset.data.shift());
  }
  state.chart.update();
  $("sampleCount").textContent = `${state.chart.data.labels.length} samples`;
  setVisible($("chartEmpty"), state.chart.data.labels.length === 0);
}

function renderAiGrid() {
  $("aiGrid").innerHTML = Array.from({ length: 8 }, (_, index) => {
    const id = `sensor.industrial_controller_ai_${index}`;
    return `
      <div class="col-6">
        <div class="io-tile${isUnavailable(id) ? " text-secondary" : ""}">
          <div class="label">AI${index}</div>
          <div class="value">${formatInteger(entityValue(id))}</div>
        </div>
      </div>`;
  }).join("");
}

function renderBits(element, value, count, unavailable = false) {
  const number = Number(value);
  element.innerHTML = Array.from({ length: count }, (_, index) => {
    const active = Number.isFinite(number) && ((number >> index) & 1) === 1;
    const classes = ["bit", active ? "active" : "", unavailable ? "unavailable" : ""].filter(Boolean).join(" ");
    return `<span class="${classes}">${index}</span>`;
  }).join("");
}

function renderRelayBits(value) {
  const number = Number(value);
  const unavailable = isUnavailable(ENTITY.relay);
  $("relayBits").innerHTML = Array.from({ length: 8 }, (_, index) => {
    const active = Number.isFinite(number) && ((number >> index) & 1) === 1;
    const classes = ["bit", active ? "active" : "", unavailable ? "unavailable" : ""].filter(Boolean).join(" ");
    const disabled = state.commandPending || unavailable ? " disabled" : "";
    return `<button type="button" class="${classes}" data-relay="${index}" aria-pressed="${active}"`
      + ` aria-label="切换继电器 ${index + 1}" title="切换继电器 ${index + 1}"${disabled}>${index}</button>`;
  }).join("");
}

function renderTelemetryTable() {
  const rows = [
    [ENTITY.linkState, "链路状态", (value) => value],
    [ENTITY.linkAge, "链路延迟", (value) => `${formatInteger(value)} ms`],
    [ENTITY.rtd, "RTD 温度", () => formatMeasurement(ENTITY.rtd, 2, " °C")],
    [ENTITY.supply24, "24V 电源", () => formatMeasurement(ENTITY.supply24, 2, " V")],
    [ENTITY.supply5, "5V 电源", () => formatMeasurement(ENTITY.supply5, 2, " V")],
    [ENTITY.di, "DI 位图", (value) => formatBitField(value, 2)],
    [ENTITY.relay, "继电器位图", (value) => formatBitField(value, 2)],
    [ENTITY.fault, "故障位图", (value) => formatBitField(value, 4)],
    [ENTITY.wdg, "看门狗刷新", (value) => formatInteger(value)],
    [ENTITY.lastAck, "最近命令应答", (value) => value],
    [ENTITY.busFrame, "最近现场总线帧", (value) => value],
  ].map(([entity, label, format]) => {
    const raw = entityValue(entity);
    const unavailable = isUnavailable(entity);
    const stale = !unavailable && isStale(entity);
    const stamp = state.mode === "demo" ? new Date() : entityUpdatedAt(entity);
    const timeCell = unavailable
      ? "--"
      : `${formatClock(stamp)}${stale ? ' <span class="badge bg-orange-lt stale-badge">陈旧</span>' : ""}`;

    return `
    <tr>
      <td><code>${label}</code></td>
      <td>${unavailable ? "--" : format(raw)}</td>
      <td>${timeCell}</td>
    </tr>`;
  });
  $("telemetryTable").innerHTML = rows.join("");
}

function render() {
  const linkState = String(entityValue(ENTITY.linkState, "--"));
  const linkUnavailable = isUnavailable(ENTITY.linkState);
  $("linkState").textContent = linkState;
  $("linkState").className = `metric-value mt-3 ${linkUnavailable ? "text-secondary" : stateClass(linkState)}`.trim();
  $("linkMeta").textContent = linkUnavailable
    ? "等待 ESP32-S3 上报"
    : `age=${formatInteger(entityValue(ENTITY.linkAge))} ms · 重连 ${formatInteger(entityValue(ENTITY.reconnects))}`;

  $("ai0Value").textContent = formatInteger(entityValue("sensor.industrial_controller_ai_0"));
  $("aiMeta").textContent = isUnavailable("sensor.industrial_controller_ai_1")
    ? "8 路模拟输入 · 暂无数据"
    : `AI1 ${formatInteger(entityValue("sensor.industrial_controller_ai_1"))} · AI2 ${formatInteger(entityValue("sensor.industrial_controller_ai_2"))} · AI3 ${formatInteger(entityValue("sensor.industrial_controller_ai_3"))}`;

  $("rtdValue").textContent = formatMeasurement(ENTITY.rtd, 2, " °C");

  const supply24 = formatMeasurement(ENTITY.supply24, 2);
  const supply5 = formatMeasurement(ENTITY.supply5, 2);
  const supplyReady = supply24 !== "--" && supply5 !== "--";
  $("supplyValue").textContent = supplyReady ? `${supply24} / ${supply5} V` : "--";
  $("supplyMeta").textContent = supplyReady
    ? `24V ${supply24}V · 5V ${supply5}V`
    : "24V / 5V · 暂无数据";

  $("faultValue").textContent = hex(entityValue(ENTITY.fault), 4);
  $("watchdogValue").textContent = formatInteger(entityValue(ENTITY.wdg));
  $("linkAge").textContent = isUnavailable(ENTITY.linkAge)
    ? "--"
    : `${formatInteger(entityValue(ENTITY.linkAge))} ms`;
  $("reconnects").textContent = formatInteger(entityValue(ENTITY.reconnects));
  $("timeouts").textContent = formatInteger(entityValue(ENTITY.timeouts));
  $("alive").textContent = hex(entityValue(ENTITY.alive), 4);
  $("lastUpdate").textContent = `最后更新 ${new Date().toLocaleTimeString("zh-CN", { hour12: false })}`;

  const gateway = entityValue(ENTITY.gateway) === "on";
  setVisible($("offlineAlert"), state.mode === "real" && !gateway);
  if (!gateway) {
    $("offlineText").textContent = linkUnavailable
      ? "网关离线：ESP32-S3 与 STM32 均未上报数据，页面保留最后一次可用的状态快照"
      : `网关离线：设备数据可能已过期，最近一次上报 ${formatClock(entityUpdatedAt(ENTITY.linkState))}`;
  }
  setStatus(
    state.mode === "demo" ? "演示数据运行中" : gateway ? "已连接 Home Assistant" : "网关离线",
    gateway ? "online" : "offline"
  );

  renderAiGrid();
  renderBits($("diBits"), entityValue(ENTITY.di), 8, isUnavailable(ENTITY.di));
  renderRelayBits(entityValue(ENTITY.relay));
  renderTelemetryTable();
  appendChartPoint();
  refreshIcons();
}

async function refreshAccessToken() {
  const refreshToken = state.tokens?.refresh_token;
  if (!refreshToken) return false;

  const response = await fetch("/auth/token", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({
      grant_type: "refresh_token",
      refresh_token: refreshToken,
      client_id: state.tokens.clientId,
    }),
  });
  if (!response.ok) return false;

  const tokens = await response.json();
  state.tokens = {
    ...state.tokens,
    ...tokens,
    expires: Date.now() + Number(tokens.expires_in ?? 1800) * 1000,
  };
  localStorage.setItem("haTokens", JSON.stringify(state.tokens));
  return true;
}

async function haApi(path, options = {}, allowRetry = true) {
  const response = await fetch(path, {
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(state.tokens?.access_token ? { Authorization: `Bearer ${state.tokens.access_token}` } : {}),
      ...(options.headers || {}),
    },
  });
  if (response.status === 401) {
    if (allowRetry && (await refreshAccessToken().catch(() => false))) {
      return haApi(path, options, false);
    }
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
  try {
    const states = await haApi("/api/states");
    state.latest = Object.fromEntries(states.map((entity) => [entity.entity_id, entity]));
    state.pollErrors = 0;
    setVisible($("errorAlert"), false);
    render();
  } catch (error) {
    state.pollErrors += 1;
    if (state.pollErrors >= 2) {
      $("errorAlert").textContent = `无法读取 Home Assistant 状态：${error.message}`;
      setVisible($("errorAlert"), true);
    }
    throw error;
  }
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

async function currentAckMarker() {
  try {
    const entity = await haApi(`/api/states/${ENTITY.lastAck}`);
    state.latest[ENTITY.lastAck] = entity;
    return entity.attributes?.seq ?? null;
  } catch {
    return null;
  }
}

async function waitForCommandAck(commandId, marker) {
  const deadline = Date.now() + ACK_TIMEOUT_MS;

  while (Date.now() < deadline) {
    await new Promise((resolve) => setTimeout(resolve, ACK_POLL_INTERVAL_MS));
    const entity = await haApi(`/api/states/${ENTITY.lastAck}`);
    state.latest[ENTITY.lastAck] = entity;
    const attributes = entity.attributes || {};

    if (marker !== null && marker !== undefined && attributes.seq === marker) continue;
    if (commandId !== undefined && Number(attributes.command) !== commandId) continue;

    if (Number(attributes.result) !== 0) {
      const reason = attributes.reason ? `（${attributes.reason}）` : "";
      const text = RESULT_TEXT[Number(attributes.result)] ?? `结果码 ${attributes.result}`;
      throw new Error(`STM32 应答异常：${text}${reason}`);
    }
    return attributes;
  }

  throw new Error("STM32 未在 6 秒内应答，请检查链路状态与看门狗诊断");
}

async function waitForNumberState(entityId, expected, timeoutMs = STATE_TIMEOUT_MS) {
  const deadline = Date.now() + timeoutMs;
  let last = null;

  while (Date.now() < deadline) {
    const entity = await haApi(`/api/states/${entityId}`);
    state.latest[entityId] = entity;
    last = Number(entity.state);
    if (Number.isFinite(last) && last === expected) return true;
    await new Promise((resolve) => setTimeout(resolve, ACK_POLL_INTERVAL_MS));
  }

  throw new Error(`设备状态未确认（当前 ${last ?? "--"}，期望 ${expected}）`);
}

function setCommandPending(pending) {
  state.commandPending = pending;
  document.querySelectorAll(".command-button").forEach((button) => {
    button.disabled = pending;
  });
  const relayDisabled = pending || isUnavailable(ENTITY.relay);
  document.querySelectorAll("#relayBits button").forEach((button) => {
    button.disabled = relayDisabled;
  });
}

async function sendCommand(button) {
  if (state.commandPending) return;
  const command = button.dataset.command;
  const result = $("commandResult");
  result.className = "alert alert-info mb-0";
  result.textContent = `正在执行 ${command}...`;
  setCommandPending(true);
  try {
    if (state.mode === "demo") {
      await new Promise((resolve) => setTimeout(resolve, 350));
      result.className = "alert alert-success mb-0";
      result.textContent = `演示模式下 ${command} 命令只在本地模拟，未下发到设备。`;
      return;
    }
    const marker = await currentAckMarker();
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
    const ack = await waitForCommandAck(COMMAND_ID[command], marker);
    if (command === "relay") {
      await waitForNumberState(ENTITY.relayMask, Number(button.dataset.mask));
    }
    result.className = "alert alert-success mb-0";
    result.textContent = `命令 ${command} 已由 STM32 执行并确认（应答 #${ack.id ?? "--"}）。`;
    await fetchStates();
  } catch (error) {
    result.className = "alert alert-danger mb-0";
    result.textContent = `执行失败：${error.message}`;
  } finally {
    setCommandPending(false);
  }
}

async function toggleRelay(index) {
  if (state.commandPending) return;
  const bit = 1 << index;
  const current = numberValue(ENTITY.relay, 0);
  const next = current ^ bit;
  if (state.mode === "demo") {
    demoRelayMask = next;
    state.latest[ENTITY.relay] = next;
    const demoResult = $("commandResult");
    demoResult.className = "alert alert-success mb-0";
    demoResult.textContent = `演示模式：继电器 ${index + 1} 已${((next >> index) & 1) === 1 ? "吸合" : "断开"}（本地模拟）。`;
    render();
    return;
  }

  const result = $("commandResult");
  result.className = "alert alert-info mb-0";
  result.textContent = `正在切换继电器 ${index + 1}...`;
  setCommandPending(true);
  try {
    const marker = await currentAckMarker();
    await haApi("/api/services/number/set_value", {
      method: "POST",
      body: JSON.stringify({
        entity_id: ENTITY.relayMask,
        value: next,
      }),
    });
    await waitForCommandAck(COMMAND_ID.relay, marker);
    await waitForNumberState(ENTITY.relayMask, next);
    result.className = "alert alert-success mb-0";
    result.textContent = `继电器 ${index + 1} 已确认${((next >> index) & 1) === 1 ? "吸合" : "断开"}。`;
    await fetchStates();
  } catch (error) {
    result.className = "alert alert-danger mb-0";
    result.textContent = `继电器 ${index + 1} 切换失败：${error.message}`;
  } finally {
    setCommandPending(false);
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
  state.latest[ENTITY.relay] = demoRelayMask;
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
$("relayBits").addEventListener("click", (event) => {
  const button = event.target.closest("[data-relay]");
  if (!button) return;
  toggleRelay(Number(button.dataset.relay));
});

createChart();
refreshIcons();
setVisible($("chartEmpty"), true);
restoreSession();

document.addEventListener("visibilitychange", () => {
  if (!document.hidden && state.mode === "real") fetchStates().catch(() => {});
});

setInterval(() => {
  if (state.mode !== "real" || document.hidden) return;
  fetchStates().catch(() => {});
}, 2000);
