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
industrial/ack             command result JSON
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
