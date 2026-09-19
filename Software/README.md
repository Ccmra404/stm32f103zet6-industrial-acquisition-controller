# 软件架构

本文说明 STM32、ESP32-S3 和共享协议的分工。软件分为两个独立固件，通过 UART 交换状态、事件和命令。

## 设计目标

- STM32 独立完成采样、报警、继电器控制和现场通信。网络断开不影响本地控制。
- ESP32-S3 只通过命令请求物理输出，不直接操作 STM32 的 GPIO。
- 每个状态只有一个写入方，其他模块读取快照。
- 协议编解码不依赖 HAL、FreeRTOS 或 ESP-IDF，能够在主机上测试。
- 网络、显示和音频任务不能阻塞 UART 接收与命令确认。

## 运行边界

| 项目 | STM32F103ZET6 | ESP32-S3 |
| --- | --- | --- |
| 实时控制 | 全部 | 不参与 |
| 模拟采集 | ADS1256、MAX31865、电源监测 | 只显示结果 |
| 数字量 | 数字输入、继电器、模拟输出 | 只发送命令和显示状态 |
| 现场通信 | RS485、RS232、CAN | 不直接连接现场总线 |
| 配置存储 | 校准值、设备参数 | WiFi、云端和界面配置 |
| 人机界面 | 状态灯 | LCD、音频 |
| 网络 | 不直接联网 | WiFi、MQTT、WebSocket、OTA |
| 板间连接 | UART1 | UART1 |

## 总体结构

```text
                        共享协议模块
                     frame / payload / CRC
                         /            \
                        /              \
               STM32 固件              ESP32-S3 固件
          ┌──────────────────┐    ┌──────────────────┐
          │ 应用逻辑          │    │ 状态缓存          │
          │ 设备状态          │    │ 命令路由          │
          │ 数据采集          │    │ LCD 与音频        │
          │ 输出控制          │    │ 网络与 OTA        │
          │ 配置与诊断        │    │ 配置与诊断        │
          ├──────────────────┤    ├──────────────────┤
          │ HAL / FreeRTOS   │    │ ESP-IDF / NVS    │
          └──────────────────┘    └──────────────────┘
```

## STM32 模块

| 模块 | 职责 | 主要接口 |
| --- | --- | --- |
| `board` | 时钟、引脚、中断和启动顺序 | `board_init()` |
| `ads1256` | 配置、采样、状态和校准 | `ads1256_read_scan()` |
| `max31865` | PT100 或 PT1000 温度采集 | `max31865_read()` |
| `digital_input` | 扫描、消抖和边沿事件 | `di_poll()`、`di_snapshot()` |
| `relay_output` | 安全启动、掩码更新和回读 | `relay_apply()`、`relay_state()` |
| `analog_output` | DAC 输出、标定和范围检查 | `ao_set()`、`ao_state()` |
| `power_monitor` | 24V、5V 监测和掉电判断 | `power_sample()` |
| `config_store` | EEPROM 参数、版本和校验 | `config_load()`、`config_save()` |
| `device_state` | 保存统一设备快照 | `state_publish()`、`state_snapshot()` |
| `bridge_uart` | 板间协议、超时和心跳 | `bridge_poll()`、`bridge_send()` |
| `field_comm` | RS485、RS232 和 CAN | `field_send()`、`field_poll()` |
| `diagnostics` | 错误计数、复位原因和运行时间 | `diag_snapshot()` |

## ESP32-S3 模块

| 模块 | 职责 | 主要接口 |
| --- | --- | --- |
| `bridge_uart` | 接收、解析、发送和超时检测 | `bridge_init()`、`bridge_poll()` |
| `state_cache` | 保存最近一次 STM32 状态 | `state_update()`、`state_get()` |
| `command_router` | 请求编号、确认、超时和重试 | `command_submit()` |
| `lcd_ui` | 状态页、报警页和参数页 | `ui_render()` |
| `audio_service` | ES8311、NS4150B 和提示音 | `audio_play()` |
| `network_service` | WiFi、MQTT、WebSocket | `network_publish()` |
| `config_store` | 使用 NVS 保存网络和界面配置 | `config_get()`、`config_set()` |
| `ota_service` | 固件下载、校验和回滚 | `ota_check()`、`ota_apply()` |
| `diagnostics` | 网络、UART、CRC 和命令统计 | `diag_snapshot()` |

## 状态管理

STM32 是设备状态的唯一写入方。采集任务把结果发布到 `device_state`，协议任务发送快照。LCD 和网络模块只读取快照，不直接读取 ADC 或 GPIO。

ESP32-S3 缓存最近一次完整状态。UART 超过 3 秒没有有效帧时，界面标记 STM32 离线。STM32 不等待 ESP32，继续采样、报警和控制。

命令路径如下：

```text
LCD 或网络
    |
    v
command_router
    |
    v
bridge_uart -> STM32 bridge_uart -> command handler
                                      |
                                      v
                               relay / analog output
                                      |
                                      v
                                  command ACK
```

只有收到对应的 ACK 后，ESP32-S3 才更新命令状态。重试必须复用同一个 `request_id`。

## STM32 任务

STM32 使用 CubeMX 生成 HAL 初始化，并配置 FreeRTOS。任务按职责划分，优先级从高到低如下。

| 任务 | 典型周期 | 职责 |
| --- | --- | --- |
| `control_task` | 1 ms 到 5 ms | 数字输入消抖、继电器状态机和安全联锁 |
| `acq_task` | DRDY 触发 | ADS1256 采样、原值校准和状态发布 |
| `rtd_task` | 100 ms | MAX31865 读取和故障检测 |
| `bridge_task` | 事件触发 | UART 帧解析、心跳、遥测和命令处理 |
| `field_task` | 事件触发 | RS485、RS232 和 CAN 数据交换 |
| `storage_task` | 按请求 | EEPROM 读取、写入和校验 |
| `monitor_task` | 50 ms | 24V、5V、堆栈和错误计数 |

高优先级任务只做时间敏感工作。EEPROM 写入、日志和网络相关处理放在低优先级任务。

## ESP32-S3 任务

| 任务 | 优先级 | 职责 |
| --- | --- | --- |
| `bridge_task` | 高 | UART 接收、协议解析和 ACK 路由 |
| `ui_task` | 中 | 20 Hz 到 30 Hz 刷新 LCD 状态 |
| `network_task` | 中 | WiFi、MQTT、WebSocket 和状态发布 |
| `audio_task` | 中 | I2S 播放和音频事件 |
| `ota_task` | 低 | 固件下载、校验和升级 |

`bridge_task` 使用独立接收缓冲和事件队列。显示刷新不得在 UART 回调中执行。网络线程不持有设备状态锁。

## 安全策略

- 上电时默认关闭全部继电器，不恢复上次输出状态。
- 输出命令经过范围检查、状态检查和 ACK 确认。
- UART 无响应时，STM32 拒绝新的远程命令，继续执行本地控制策略和报警。
- ADC、温度或电源监测超出范围时产生事件，不静默丢弃。
- 配置写入包含版本、长度和 CRC。校验失败时恢复默认值。
- 命令重试使用相同 `request_id`，STM32 对重复请求返回同一结果。

## 构建目录

```text
Firmware/
  shared/
    protocol/
  stm32/
    Core/
    App/
    Drivers/
    MDK-ARM/
  esp32/
    main/
    components/
  tools/
    protocol_vectors/
  tests/
```

`shared/protocol` 只使用 C 标准库。STM32 和 ESP32 都编译同一份协议代码，避免两端字段布局不一致。

## 实施顺序

1. 建立共享协议模块和主机测试。
2. 在 STM32 上完成 UART 回环、心跳和假遥测。
3. 在 ESP32-S3 上完成帧解析、状态缓存和 LCD 数据页。
4. 接入 ADS1256、MAX31865、数字输入、继电器和电源监测。
5. 接入 EEPROM、校准和配置校验。
6. 接入 RS485、RS232 和 CAN。
7. 接入 MQTT、WebSocket、音频和 OTA。
8. 执行断网、掉电、CRC 错误和命令重试测试。

板间帧格式和消息定义见[板间协议](PROTOCOL.md)。CubeMX 的引脚、时钟、DMA 和 FreeRTOS 配置见 [STM32CubeMX 配置清单](CUBEMX.md)。
