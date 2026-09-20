# Home Assistant + Mushroom

This stack replaces the ThingsBoard dashboard with a self-hosted Home Assistant
instance and a Mushroom dashboard.

## Architecture

```text
STM32F103ZET6
      |
      | UART 115200
      v
ESP32-S3 gateway
      |
      | MQTT
      v
Mosquitto
      |
      +--> Home Assistant MQTT Discovery
      |         |
      |         +--> sensors, binary sensors, buttons, number entity
      |
      +--> Mushroom dashboard
```

The ESP32 publishes retained Home Assistant discovery documents after every
MQTT connection. Home Assistant creates the device and entities automatically.
The dashboard file only defines the visual layout.

## 阿里云部署

当前项目已部署在阿里云轻量应用服务器：

```text
自写控制台      http://<server-public-ip>:8888/
MQTT broker     mqtt://<server-public-ip>:22022
```

`8888` 由 Nginx 提供自写 Tabler 控制台，并把 `/auth`、`/api` 反代到
Home Assistant 的 `8123`。Mosquitto 直接监听 `22022`。云端只开放已经验证
可用的这两个端口。

服务器目录：

```text
/opt/industrial-controller
```

更新容器：

```bash
cd /opt/industrial-controller
export DOCKER_HOST=unix:///run/podman/podman.sock
docker compose up -d
```

Home Assistant 账号为 `admin`。密码保存在本机 Codex 私密目录：

```text
C:\Users\zalry\.codex\.sandbox-secrets\home-assistant-owner.json
```

自写控制台和 Mushroom 原生 Dashboard 都使用同一批 Home Assistant 实体。
自写控制台的继电器位图支持逐路点击，可单独切换 0 到 7 路输出。
控制台下发命令后会等待 `industrial/ack` 上新出现的 `seq`，只有 STM32 返回
`result = 0` 才提示成功，随后回读继电器掩码确认输出状态；超时或异常码会直接
显示原因，不会把 Home Assistant 接受请求当成设备已执行。

页面状态处理：

- 网关离线或实体为 `unavailable` 时统一显示 `--` 并给出黄色离线横幅，不再把原始
  `unavailable` 字符串铺满界面。
- 遥测表按字段格式化（温度/电压保留两位小数，位图显示十六进制与十进制），时间列
  使用实体自身 `last_updated`，超过 15 秒未更新会标注“陈旧”。
- 访问令牌过期时用 `refresh_token` 自动续期，续期失败才回到登录页。
- 页面隐藏时暂停轮询，重新可见时立即刷新一次。

控制台已覆盖的设备操作：

| 区域 | 能力 |
| --- | --- |
| 继电器 | 8 路逐位点击切换、全部吸合 / 全部断开、任意通道 100 - 60000 ms 脉冲 |
| 模拟输出 | DAC1 / DAC2 滑条与数值输入，0 - 4095，执行后回读遥测确认 |
| 配置 | 保存配置、加载配置、清除锁存故障 |
| 现场总线 | 最近 RS232 / CAN 帧、仲裁标识、长度、十六进制数据和累计帧数 |
| 故障 | 故障位图按位展开为中文名称，无故障时明确显示“无故障” |
| 记录 | 本浏览器最近 20 条命令的时间、结果和说明，可一键清空 |

脉冲和模拟输出通过 Home Assistant 的 `mqtt.publish` 服务直接发到
`industrial/command`，因此通道和时长不受 MQTT Discovery 固定载荷限制，仍然按同一套
`seq` 应答规则确认执行结果。

## Connect the ESP32

在 ESP32 控制台执行：

```text
mqtt mqtt://<server-public-ip>:22022
mqttauth industrial <mqtt-password>
token off
tb off
reconnect
```

MQTT 密码保存在：

```text
C:\Users\zalry\.codex\.sandbox-secrets\industrial-controller-mqtt.json
```

After the ESP32 reconnects, Home Assistant should show a new device named
`工业采集控制终端` under MQTT.

## MQTT Topics

```text
industrial/telemetry       telemetry JSON
industrial/event           link-state event JSON
industrial/command         command JSON
industrial/ack             command result JSON (含递增 seq)
industrial/bus             RS232 / CAN 接收帧 JSON
industrial/availability    online / offline
homeassistant/...          retained discovery documents
```

## Security

The local Mosquitto configuration requires username and password authentication.
Before exposing the broker outside the trusted LAN:

1. Use a long random MQTT password.
2. Configure TLS.
3. Set the ESP32 command token and adapt the Home Assistant command entities.
4. Restrict the MQTT port in the cloud firewall to known source addresses when possible.

The current Home Assistant control buttons expect `token off`. If the ESP32
command token is enabled, add a Home Assistant script layer that injects the
token into the MQTT command payload.
