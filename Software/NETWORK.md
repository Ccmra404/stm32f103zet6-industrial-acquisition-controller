# WiFi 与 MQTT

ESP32-S3 使用运行时配置连接 WiFi 和 MQTT。仓库不保存 WiFi 密码、Broker 地址或证书，配置写入 NVS。

## 控制台配置

以下命令把 SSID 和密码保存到 NVS，并触发网络重连：

```text
wifi <ssid> <password>
```

开放网络使用 `-` 作为密码：

```text
wifi MyNetwork -
```

配置 MQTT Broker：

```text
mqtt mqtts://broker.emqx.io:8883
```

配置 MQTT 用户名和密码：

```text
mqttauth <username> <password>
```

关闭 MQTT 认证：

```text
mqttauth off
```

设置远程命令令牌：

```text
token <token>
```

关闭命令令牌：

```text
token off
```

设置 Web 控制台登录凭据：

```text
webuser <username> <password>
```

关闭 Web 控制台登录：

```text
webuser off
```

切换到 ThingsBoard Cloud：

```text
tb <device-access-token>
```

返回自定义 MQTT 协议：

```text
tb off
```

ThingsBoard 模式使用：

```text
Broker:   mqtts://mqtt.thingsboard.cloud:8883
Username: device access token
Password: empty
Telemetry: v1/devices/me/telemetry
RPC request: v1/devices/me/rpc/request/+
RPC response: v1/devices/me/rpc/response/<request-id>
```

支持的 RPC 方法：

```text
relay / setRelay
pulse
dac / setDac
clear / clearFaults
save
load
```

MQTT TLS 使用 ESP-IDF X.509 证书包校验 Broker 证书。连接私有 Broker 时仍需按现场证书链补充 CA、用户名和密码。

查询网络状态：

```text
status
```

查询最近的链路状态事件：

```text
events
```

查询最近的命令审计记录：

```text
audit
```

手动应用已保存配置：

```text
reconnect
```

## 遥测发布

WiFi 连接完成后，ESP32-S3 创建 MQTT 客户端。MQTT 连接成功后，每 2 秒向以下主题发布一条 JSON：

```text
industrial/telemetry
```

载荷示例：

```json
{
  "ai_raw": [0, 10, 20, 30, 40, 50, 60, 70],
  "rtd_mc": 25123,
  "di": 5,
  "relay": 1,
  "supply_mv": [24000, 5000],
  "fault": 0,
  "alive": 63,
  "wdg": 120,
  "link_state": "ONLINE",
  "link_age_ms": 42,
  "reconnects": 1,
  "timeouts": 1
}
```

字段说明：

| 字段 | 含义 |
| --- | --- |
| `ai_raw` | ADS1256 八通道原始值 |
| `rtd_mc` | RTD 温度，单位 0.001 摄氏度 |
| `di` | 数字输入位图 |
| `relay` | 继电器输出位图 |
| `supply_mv` | 24V、5V，单位 mV |
| `fault` | 故障位图 |
| `alive` | FreeRTOS 任务存活位图 |
| `wdg` | IWDG 刷新次数 |
| `link_state` | STM32 链路状态：`BOOT`、`ONLINE`、`DEGRADED` 或 `OFFLINE` |
| `link_age_ms` | 距离最近一次有效 STM32 帧的时间 |
| `reconnects` | 链路从离线恢复到在线次数 |
| `timeouts` | 链路进入离线状态次数 |

链路状态发生变化时，ESP32-S3 还会向以下主题发布事件：

```text
industrial/event
```

示例：

```json
{
  "event": "link_state",
  "state": "OFFLINE",
  "age_ms": 3024,
  "reconnects": 0,
  "timeouts": 1
}
```

## MQTT 命令通道

ESP32-S3 订阅以下主题接收远程命令：

```text
industrial/command
```

命令格式：

```json
{"id":101,"command":"relay","mask":0}
{"id":102,"command":"pulse","channel":0,"duration_ms":1000}
{"id":103,"command":"dac","channel":0,"value":2048}
{"id":104,"command":"clear","mask":15}
{"id":105,"command":"save"}
{"id":106,"command":"load"}
```

执行结果发布到：

```text
industrial/ack
```

示例：

```json
{"id":101,"command":1,"result":0,"detail":0,"seq":42}
```

`seq` 是 ESP32-S3 转发 ACK 时递增的应答序号。控制台先用 `seq` 判断是否为本次命令的新应答，再检查 `result` 和 `detail`，最后回读继电器掩码确认输出状态，避免把“Home Assistant 已接受请求”误判成“STM32 已执行”。

链路不在 `ONLINE` 时，命令会被拒绝并返回 `result=4`，STM32 不会收到新的输出命令。

配置命令令牌后，每个命令必须携带：

```json
{"id":101,"token":"device-token","command":"relay","mask":0}
```

输出类命令的最小间隔为 `100 ms`，超过限制会返回 `result=3`。最近 16 条命令会进入本地审计环形缓存。

## 现场总线接收

STM32 收到的 RS232 数据和 CAN 报文通过 `BUS_RX` 桥接消息上报后，ESP32-S3 发布到：

```text
industrial/bus
```

示例：

```json
{"bus":"rs232","id":0,"length":5,"data":"48 65 6C 6C 6F","count":3}
{"bus":"can","id":291,"length":8,"data":"10 20 30 40 50 60 70 80","count":7}
```

`count` 为对应总线上电后的累计接收帧数，可用于确认接收路径是否在工作。Home Assistant 会自动发现 `sensor.industrial_controller_last_bus_frame`，属性中包含 `id`、`length`、`data` 和 `count`。

## Home Assistant 自动发现

自定义 MQTT 模式下，ESP32-S3 会在 MQTT 连接成功后发布 Home Assistant
MQTT Discovery 配置。配置使用 retained 消息，因此 Home Assistant 重启后仍能恢复
设备和实体。

发现主题前缀：

```text
homeassistant/+/industrial_controller_esp32/+/config
```

可用性主题：

```text
industrial/availability
```

ESP32-S3 连接成功后发布 `online`，断开连接时由 MQTT Last Will 发布
`offline`。Home Assistant 自动创建设备、遥测传感器、故障二进制传感器、命令按钮
和继电器掩码控制实体。

可用的 Mushroom 控制面板、Mosquitto 和 Docker Compose 配置位于：

```text
Software/home-assistant
```

首次联调时建议关闭命令令牌：

```text
token off
```

这样可以先验证 MQTT Discovery、实体和 Dashboard，再单独接入命令令牌和 TLS。

## 本地 Web 控制台

ESP32-S3 在局域网中同时提供 HTTPS 控制台，设备启动并连接 WiFi 后可直接访问：

```text
https://<ESP32-IP>/
```

当前使用 `Firmware/esp32/main/certs/` 中的局域网自签名证书，浏览器首次访问需要确认信任。正式部署时应替换为受信任 CA 签发的证书。

接口：

```text
GET  /api/status   链路、遥测、网络和安全状态
GET  /api/events   最近链路状态事件
GET  /api/audit    最近命令审计记录
POST /api/command  发送带 token 的 JSON 命令
POST /api/login    Web 登录
POST /api/logout   退出 Web 登录
```

页面支持查看模拟量、数字量、电源、链路状态、重连统计和命令审计，并提供 `relay`、`pulse`、`dac`、`clear`、`save`、`load` 操作。浏览器中的设备令牌只保存在当前页面内存中，不写入持久存储。

登录成功后会生成 30 分钟有效的服务端会话 Cookie；未登录访问状态、事件、审计和命令接口均返回 `401`。退出登录会立即清除服务端会话。

## 软件任务

`network` 任务负责：

1. 读取 NVS 中的 WiFi 和 MQTT 配置。
2. 启动 WiFi Station 并处理连接事件。
3. 创建 MQTT 客户端并处理连接状态。
4. 从 `s_telemetry` 和 `s_diagnostics` 读取一致快照。
5. 发布 JSON 遥测。

网络任务优先级低于 UART 接收和命令路由，不能阻塞 STM32 的实时控制路径。
