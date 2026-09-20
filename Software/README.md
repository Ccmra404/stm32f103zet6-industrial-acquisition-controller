# 软件架构

本文说明 STM32、ESP32-S3 和共享协议的分工。软件分为两个独立固件，通过 UART 交换状态、事件和命令。

当前固件已经实现实时控制、桥接、状态缓存和命令确认。WiFi、MQTT、OTA、LCD 和音频属于扩展接口，不参与当前构建。

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
| 模拟采集 | ADS1256、MAX31865、电源监测 | 接收遥测结果，不直接采样 |
| 数字量 | 数字输入、继电器、模拟输出 | 提供命令发送入口，不直接操作 GPIO |
| 现场通信 | RS485、RS232、CAN | 不直接连接现场总线 |
| 配置存储 | 校准值和设备参数 | 初始化 NVS，网络配置留作扩展 |
| 人机界面 | 状态灯 | LCD 和音频接口留作扩展 |
| 网络 | 不直接联网 | WiFi、MQTT 和 OTA 留作扩展 |
| 板间连接 | UART1 | UART1 |

## FreeRTOS 运行框架

STM32 使用 FreeRTOS 和 CMSIS-RTOS V2。系统节拍为 `1 ms`，开启抢占式调度。任务按实时性设置为 `High`、`AboveNormal` 和 `Normal`。

| 配置 | 值 | 作用 |
| --- | ---: | --- |
| `configTICK_RATE_HZ` | `1000` | 提供 1 ms 调度节拍 |
| `configUSE_PREEMPTION` | `1` | 高优先级任务就绪后立即抢占 |
| `configUSE_MUTEXES` | `1` | 保护状态快照 |
| `configUSE_TIMERS` | `1` | 执行继电器脉冲超时 |
| `configTOTAL_HEAP_SIZE` | `12288` | 为任务、队列、互斥锁和定时器提供动态内存 |

`monitorTask` 周期检查 6 个业务任务的存活时间和栈余量。全部任务健康时刷新 IWDG，任一任务超时后停止刷新，让硬件看门狗复位控制器。

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
| `app` | 任务创建、事件队列、命令处理和周期调度 | `App_Init()`、`App_CreateTasks()` |
| `ads1256` | 配置、采样和状态 | `ADS1256_ReadAll()` |
| `max31865` | PT100 或 PT1000 温度采集 | `MAX31865_ReadMilliCelsius()` |
| `digital_input` | 扫描和消抖 | `DigitalInput_Update()`、`DigitalInput_GetStableBits()` |
| `relay_output` | 安全启动、掩码更新和回读 | `RelayOutput_SetMask()`、`RelayOutput_GetMask()` |
| `analog_output` | DAC 输出和范围检查 | `AnalogOutput_SetRaw()`、`AnalogOutput_GetRaw()` |
| `config_store` | EEPROM 参数、版本和校验 | `ConfigStore_Load()`、`ConfigStore_Save()` |
| `device_state` | 保存统一设备快照 | `DeviceState_Update*()`、`DeviceState_Get()` |
| `bridge_protocol` | 帧构建、解析、CRC 和命令编解码 | `BridgeProtocol_*()` |
| `field_comm` | RS485、RS232 和 CAN 发送 | `FieldComm_SendRs485()`、`FieldComm_SendCan()` |
| `modbus_rtu` | Modbus RTU 从站、寄存器映射和异常响应 | `ModbusRtu_Process()` |
| `monitorTask` | 24V、5V 监测和状态灯 | `DeviceState_UpdateSupplies()` |

## ESP32-S3 当前模块

| 模块 | 职责 | 主要接口 |
| --- | --- | --- |
| `uart_rx` | 读取 UART1 并逐字节解析 | `UartRxTask()` |
| `heartbeat` | 发送 HELLO 和心跳，判断链路离线 | `HeartbeatTask()` |
| `command_router` | 发送命令、等待 ACK、超时重试 | `CommandRouterTask()` |
| `console` | 解析 UART0 控制台命令 | `ConsoleTask()` |
| `state cache` | 保存最近一次遥测和在线状态 | `s_telemetry`、`s_state_lock` |
| `bridge_protocol` | 构建和解析协议帧 | `BridgeProtocol_*()` |
| `diagnostics` | 任务活性、栈余量和错误计数 | `DIAGNOSTICS` 帧 |

以下模块属于扩展接口，当前固件不初始化：

| 模块 | 计划职责 |
| --- | --- |
| `network_service` | WiFi、MQTT、WebSocket 和远程状态发布 |
| `ota_service` | 固件下载、校验和升级 |
| `lcd_ui` | 状态页、报警页和参数页 |
| `audio_service` | ES8311、NS4150B 和提示音 |

## 状态管理

STM32 是设备状态的唯一写入方。采集任务把结果发布到 `device_state`，协议任务发送快照。扩展界面和网络模块只读取快照，不直接读取 ADC 或 GPIO。

ESP32-S3 缓存最近一次完整状态。UART 超过 3 秒没有有效帧时，界面标记 STM32 离线。STM32 不等待 ESP32，继续采样、报警和控制。

扩展命令路径如下：

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

当前固件收到 `COMMAND_ACK` 后解析请求编号、命令编号、结果和补充信息。扩展业务层在收到 `OK` 后更新命令状态。

## STM32 任务

STM32 使用 CubeMX 生成 HAL 初始化，并配置 FreeRTOS。任务按职责划分，优先级从高到低如下。

| 任务 | FreeRTOS 优先级 | 周期 | 职责 |
| --- | --- | ---: | --- |
| `controlTask` | `High` | 5 ms | 数字输入消抖、继电器状态同步和输入事件 |
| `acqTask` | `High` | 100 ms | ADS1256 八通道采样和状态发布 |
| `monitorTask` | `AboveNormal` | 50 ms | 24V、5V 监测和状态灯 |
| `rtdTask` | `AboveNormal` | 500 ms | MAX31865 读取和故障检测 |
| `bridgeTask` | `Normal` | 10 ms 循环 | UART 帧解析、心跳、遥测、事件和命令处理 |
| `modbusTask` | `Normal` | 2 ms 轮询 | RS485 Modbus RTU 收帧、寄存器读写和输出执行 |

中断和任务之间通过队列解耦。UART 空闲 DMA 回调只把接收字节投递到 `s_uart_rx_queue`，协议解析和命令处理由 `bridgeTask` 完成。

## 任务间通信

| 机制 | 配置 | 用途 |
| --- | ---: | --- |
| `s_uart_rx_queue` | 512 字节 | 在中断和 `bridgeTask` 之间传递 UART 数据 |
| `s_event_queue` | 16 条消息 | 传递输入变化和继电器变化事件 |
| `deviceStateMutex` | 普通互斥锁 | 保护统一设备快照 |
| `osTimerOnce x8` | 8 个 | 继电器脉冲到期后撤销输出 |
| `s_command_queue` | 8 条命令 | 在控制台和命令路由之间传递请求 |
| `s_ack_queue` | 8 条确认 | 保存命令结果并结束超时等待 |
| `portMUX` | ESP32 临界区 | 保护在线状态和最后接收时间 |

## ESP32-S3 任务

| 任务 | 优先级 | 职责 |
| --- | --- | --- |
| `uart_rx` | 8 | UART 接收、协议解析和消息分发 |
| `heartbeat` | 6 | HELLO、心跳和 3 秒离线判断 |
| `command_router` | 6 | 命令发送、ACK 匹配和超时重试 |
| `console` | 4 | UART0 命令行控制和状态查询 |

`uart_rx` 使用独立接收缓冲，协议解析不放在中断中。`heartbeat` 使用临界区保护在线状态和最后接收时间。

## 安全策略

- 上电时默认关闭全部继电器，不恢复上次输出状态。
- 输出命令经过范围检查，并由 `COMMAND_ACK` 返回执行结果。
- UART 无响应时，STM32 继续执行本地控制策略和报警。
- ADC、温度或电源监测超出范围时产生事件，不静默丢弃。
- 配置写入包含版本、长度和 CRC。校验失败时恢复默认值。
- ESP32 命令在 500 ms 无 ACK 时复用同一 `request_id` 重试。
- STM32 缓存最近 16 条请求结果，重复请求不会再次执行输出。

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
7. 接入 WiFi、MQTT、WebSocket、LCD、音频和 OTA。
8. 执行断网、掉电、CRC 错误和命令重试测试。

板间帧格式和消息定义见[板间协议](PROTOCOL.md)。CubeMX 的引脚、时钟、DMA 和 FreeRTOS 配置见 [STM32CubeMX 配置清单](CUBEMX.md)。
