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
mqtt mqtt://192.168.1.10:1883
```

使用 TLS 时填写 `mqtts://` 地址。当前代码使用 MQTT 客户端默认安全配置，生产环境需要补充 CA 证书和认证信息。

查询网络状态：

```text
status
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
  "wdg": 120
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

## 软件任务

`network` 任务负责：

1. 读取 NVS 中的 WiFi 和 MQTT 配置。
2. 启动 WiFi Station 并处理连接事件。
3. 创建 MQTT 客户端并处理连接状态。
4. 从 `s_telemetry` 和 `s_diagnostics` 读取一致快照。
5. 发布 JSON 遥测。

网络任务优先级低于 UART 接收和命令路由，不能阻塞 STM32 的实时控制路径。
