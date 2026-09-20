# 基于 STM32F103ZET6 与 ESP32-S3 的工业采集控制终端

<p align="center">
  <strong>STM32 负责实时采集与控制，ESP32-S3 负责桥接、联网和远程服务</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F103ZET6-1F4E79" alt="STM32F103ZET6">
  <img src="https://img.shields.io/badge/Wireless-ESP32--S3-2D7F72" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/RTOS-FreeRTOS-6E5AA8" alt="FreeRTOS">
  <img src="https://img.shields.io/badge/Framework-ESP--IDF%206.1-2D7F72" alt="ESP-IDF 6.1">
  <img src="https://img.shields.io/badge/ADC-ADS1256-C46210" alt="ADS1256">
  <img src="https://img.shields.io/badge/Protocol-CRC--16-B14E4A" alt="CRC-16 Protocol">
  <img src="https://github.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/actions/workflows/build.yml/badge.svg" alt="Build">
  <a href="https://github.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/releases/tag/v1.0.0"><img src="https://img.shields.io/badge/Release-v1.0.0-1F6FEB" alt="Release v1.0.0"></a>
</p>

<p align="center">
  <a href="#项目能力">项目能力</a> |
  <a href="#软件系统">软件系统</a> |
  <a href="#通信协议">通信协议</a> |
  <a href="#硬件系统">硬件系统</a> |
  <a href="#构建与验证">构建与验证</a> |
  <a href="#项目资料">项目资料</a>
</p>

<p align="center">
  <a href="Documentation/images/home-assistant-dashboard.png">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/home-assistant-dashboard.png?v=dashboard1" width="100%" alt="工业采集控制终端自写控制台">
  </a>
</p>

## 项目定位

这是一套面向工业采集与控制场景的双处理器终端。STM32F103ZET6 运行 FreeRTOS，承担采样、控制、报警、存储和现场总线任务。ESP32-S3 运行 ESP-IDF，负责板间桥接、状态缓存和远程服务入口。

项目不依赖单一主控完成全部工作。实时链路保留在 STM32，网络和界面任务放在 ESP32-S3。两端通过共享二进制协议通信，网络异常不会阻塞本地采样和控制。

## 项目能力

| 层面 | 当前实现 |
| --- | --- |
| 实时软件 | FreeRTOS 1 ms tick、抢占式调度、CMSIS-RTOS V2、6 个业务任务、任务活性监督、IWDG、ADC DMA、UART DMA 空闲接收和继电器脉冲定时器 |
| 桥接软件 | UART1 解析、命令队列、ACK 重试、串口控制台、HTTPS Web 控制台、WiFi Station、MQTT 遥测和 Home Assistant Discovery |
| 通信协议 | `AA 55` 帧头、版本、消息类型、序号、长度、256 字节载荷和 CRC-16 |
| 状态管理 | `device_state` 统一保存设备快照，使用 mutex 保证任务读取一致性 |
| 采集控制 | 8 路 24 位模拟采集、PT100 或 PT1000 温度采集、8 路隔离数字输入、8 路继电器和 2 路模拟输出 |
| 现场通信 | RS485、RS232 和 CAN，收发器与控制侧隔离；RS232 与 CAN 中断接收后上报到网络侧 |
| Modbus RTU | RS485 从站（`0x03`、`0x06`、`0x10`）与主站请求编解码库，主机侧单元测试覆盖 |
| 监控界面 | 自写 Tabler 控制台、Home Assistant 实体模型和 Mushroom 原生 Dashboard |
| 工程实现 | STM32 与 ESP32-S3 两套固件独立构建，共享协议和 Modbus 通过 GitHub Actions 自动测试 |

已完成代码级功能：

- STM32 实时采集、控制、状态发布、事件上报和命令处理。
- ESP32-S3 遥测接收、状态缓存、心跳发送、在线判断、命令队列和 ACK 重试。
- ESP32-S3 通过 WiFi 连接 MQTT Broker，每 2 秒发布一次 JSON 遥测。
- ESP32-S3 自动发布 Home Assistant MQTT Discovery，创建遥测、诊断和控制实体。
- 自写 Tabler 控制台通过 Home Assistant API 显示实时数据并下发控制命令。
- 继电器掩码、8 路任意时长脉冲、双路模拟输出、故障清除和配置保存命令，模拟输出与继电器都按回读值确认。
- EEPROM 参数结构包含 `magic`、`version` 和 `crc`，校验失败时回退默认值。
- STM32 保存最近 16 条命令结果，重复请求不会再次执行输出。
- 任务活性监督、栈余量、UART 丢包和事件队列丢包可进入统一状态快照。
- STM32 提供 Modbus RTU 从站，上位机可以读取 AI、电源、温度、DI、继电器和故障位。
- STM32 以中断方式接收 RS232（UART4）和 CAN 报文，通过 `0x13 BUS_RX` 消息上报，ESP32-S3 转发到 MQTT 主题 `industrial/bus`。
- 自写控制台下发命令后等待新的 `COMMAND_ACK` 应答结果，并回读继电器掩码确认输出状态，超时或异常时显示具体原因。

保留的扩展入口：

- ESP32-S3 的 OTA 和远程参数服务。
- LCD、音频和本地人机界面。

## 软件系统

软件由 STM32 实时固件、ESP32-S3 桥接固件和共享协议模块组成。共享协议只依赖 C 标准库，避免两端字段布局不一致。

### 软件架构

<p align="center">
  <a href="Documentation/images/sw-architecture.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sw-architecture.webp?v=network1" width="100%" alt="软件系统架构图">
  </a>
</p>

### FreeRTOS 调度模型

STM32 使用 FreeRTOS 和 CMSIS-RTOS V2。系统节拍为 `1 ms`，开启抢占式调度。任务按实时性分为 `High`、`AboveNormal` 和 `Normal`，采集、控制和通信不会串行阻塞在同一个主循环里。

| FreeRTOS 配置 | 当前值 | 作用 |
| --- | ---: | --- |
| 系统节拍 | `1000 Hz` | 提供 1 ms 时间片，支持毫秒级任务调度 |
| 抢占策略 | `configUSE_PREEMPTION = 1` | 高优先级任务就绪后立即抢占低优先级任务 |
| 互斥锁 | `configUSE_MUTEXES = 1` | 保护 `device_state` 状态快照 |
| 软件定时器 | `configUSE_TIMERS = 1` | 执行 8 路继电器脉冲超时 |
| 动态内存 | `heap_4`，12 KB | 创建任务、队列、互斥锁和定时器 |
| 看门狗 | IWDG，约 4 秒 | `monitorTask` 确认全部任务存活后刷新 |

### FreeRTOS 调度与任务通信

<p align="center">
  <a href="Documentation/images/sw-task-flow.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sw-task-flow.webp?v=network1" width="100%" alt="FreeRTOS 调度和任务通信图">
  </a>
</p>

### STM32 实时任务

| 任务 | FreeRTOS 优先级 | 周期 | 主要工作 |
| --- | --- | ---: | --- |
| `controlTask` | `High` | 5 ms | 数字输入消抖、继电器同步和输入变化事件 |
| `acqTask` | `High` | 100 ms | 读取 ADS1256 八通道原始值 |
| `monitorTask` | `AboveNormal` | 50 ms | 读取 24V、5V 电压并刷新状态灯 |
| `rtdTask` | `AboveNormal` | 500 ms | 读取 MAX31865 温度并更新故障状态 |
| `bridgeTask` | `Normal` | 10 ms 循环 | 协议解析、心跳、遥测、事件和命令处理 |
| `modbusTask` | `Normal` | 2 ms 轮询 | Modbus RTU `0x03`、`0x06`、`0x10`、寄存器映射和输出命令 |

### 任务间通信

| 机制 | 配置 | 解决的问题 |
| --- | ---: | --- |
| `s_uart_rx_queue` | 512 字节 | UART 中断只投递字节，协议解析放到 `bridgeTask` |
| `s_event_queue` | 16 条消息 | 输入变化事件与遥测解耦，不覆盖历史事件 |
| `deviceStateMutex` | 普通互斥锁 | 多个任务读写状态时提供一致快照 |
| `osTimerOnce` | 8 个 | 继电器脉冲到期后自动撤销输出位 |
| `s_command_queue` | 8 条命令 | ESP32 控制台和命令路由之间的解耦 |
| `s_ack_queue` | 8 条确认 | 匹配 `request_id` 和 `command_id` 后结束重试 |
| `portMUX` | ESP32 临界区 | 保护在线状态和最后接收时间 |

### 已实现的软件亮点

- 使用 FreeRTOS 抢占式调度，把实时控制任务和网络桥接任务放在不同优先级。
- 使用 `osDelay()` 实现周期任务，不使用阻塞式忙等。
- UART 使用 DMA 空闲接收，中断只把字节投递到队列，复杂解析在任务上下文完成。
- 使用消息队列传递输入事件，事件不会被周期遥测覆盖。
- 使用互斥锁保护统一状态快照，避免任务读到半更新数据。
- 使用 8 个一次性软件定时器实现非阻塞继电器脉冲。
- `monitorTask` 检查 6 个任务的存活时间和栈余量，全部健康时才刷新 IWDG。
- UART 接收队列和事件队列记录丢包计数，异常不会被静默忽略。
- STM32 每 5 秒发送诊断帧，ESP32 可以查看任务活性、栈余量和错误计数。
- ESP32 命令采用队列发送，500 ms 无 ACK 时复用同一 `request_id` 重试两次。
- STM32 缓存最近 16 条请求结果，重复请求直接返回原结果，不重复操作继电器。
- ESP32 提供串口控制台，可直接执行继电器、脉冲、DAC、故障清除和配置命令。
- 使用独立心跳、遥测、事件和 ACK 消息，故障发生时状态不会丢失。
- 共享协议代码不依赖 HAL、FreeRTOS 或 ESP-IDF，STM32 和 ESP32-S3 使用同一份协议实现。

### ESP32-S3 桥接固件

ESP32-S3 使用 ESP-IDF 6.1。UART1 使用 `IO17 TX` 和 `IO18 RX`，波特率为 `115200`。

| 任务 | 优先级 | 主要工作 |
| --- | ---: | --- |
| `uart_rx` | 8 | 读取 UART1，使用状态机逐字节解析帧 |
| `heartbeat` | 6 | 发送 HELLO 和心跳，连续 3 秒无有效帧时判断 STM32 离线 |
| `command_router` | 6 | 发送命令、等待 ACK、超时重试并打印执行结果 |
| `console` | 4 | 通过 UART0 执行继电器、脉冲、DAC 和配置命令 |

桥接层保存最近一次遥测，使用临界区保护在线状态和最后接收时间。控制台把命令放入 `s_command_queue`，命令路由构建帧并等待 `s_ack_queue` 中的确认。

ESP32-S3 串口控制台支持：

```text
relay <mask>
pulse <channel> <milliseconds>
dac <channel> <raw-value>
clear <fault-mask>
save
load
status
wifi <ssid> <password>
mqtt <uri>
reconnect
```

### WiFi 与 MQTT

WiFi 凭据和 MQTT Broker 地址通过控制台写入 NVS，不进入仓库。MQTT 连接成功后，网络任务向 `industrial/telemetry` 发布 AI、RTD、DI、继电器、电源、故障和任务诊断数据。

配置方法和 JSON 字段见 [WiFi 与 MQTT](Software/NETWORK.md)。

### 状态一致性

STM32 是设备状态的唯一写入方。采集任务发布状态，桥接任务读取完整快照。任务不直接读取其他任务的局部变量。

```text
采集任务 / 控制任务 / 监测任务
              |
              v
        device_state
        mutex snapshot
              |
              v
         bridgeTask
```

## 通信协议

STM32 与 ESP32-S3 使用固定长度头部和变长载荷。协议支持链路发现、健康检查、遥测、事件、命令和确认。

### 帧结构

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| `SOF` | 2 字节 | 固定为 `AA 55` |
| `version` | 1 字节 | 当前版本为 `0x01` |
| `type` | 1 字节 | 消息类型 |
| `sequence` | 2 字节 | 发送序号 |
| `length` | 2 字节 | 载荷长度 |
| `payload` | 0 到 256 字节 | 消息内容 |
| `crc16` | 2 字节 | CRC-16/CCITT-FALSE |

CRC 覆盖 `version` 到 `payload`，不覆盖帧头。所有多字节字段使用小端序。

### 消息类型

| 类型 | 名称 | 方向 | 用途 |
| ---: | --- | --- | --- |
| `0x01` | `HELLO` | 双向 | 交换角色、版本和能力位 |
| `0x02` | `HEARTBEAT` | 双向 | 上报运行时间、健康状态和故障位 |
| `0x10` | `TELEMETRY` | STM32 到 ESP32-S3 | 上报采集值、输入、输出和电源状态 |
| `0x11` | `EVENT` | STM32 到 ESP32-S3 | 上报输入变化和继电器变化 |
| `0x12` | `DIAGNOSTICS` | STM32 到 ESP32-S3 | 上报任务活性、栈余量和错误计数 |
| `0x13` | `BUS_RX` | STM32 到 ESP32-S3 | 上报 RS232 数据段或 CAN 报文 |
| `0x20` | `COMMAND` | ESP32-S3 到 STM32 | 请求输出或配置操作 |
| `0x21` | `COMMAND_ACK` | STM32 到 ESP32-S3 | 返回命令结果 |

### 已实现命令

| 命令 | 功能 |
| --- | --- |
| `0x0001` | 设置继电器输出掩码 |
| `0x0002` | 脉冲指定继电器 |
| `0x0003` | 设置模拟输出通道和值 |
| `0x0004` | 清除锁存故障 |
| `0x0200` | 保存配置到 EEPROM |
| `0x0201` | 从 EEPROM 恢复配置 |

### 协议流程

<p align="center">
  <a href="Documentation/images/sw-protocol-flow.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sw-protocol-flow.webp?v=63182ca" width="100%" alt="板间协议和命令流程图">
  </a>
</p>

完整字段和命令定义见[板间协议](Software/PROTOCOL.md)。

### Modbus RTU 从站

STM32 通过 RS485 提供 Modbus RTU 从站，从站地址为 `1`。支持 `0x03`、`0x06` 和 `0x10`，寄存器覆盖 AI、电源、RTD、DI、继电器、故障位、继电器命令、DAC 和故障清除。

同一份 `modbus_rtu` 模块还提供主站侧请求构造函数和响应解析函数（`ModbusRtu_BuildReadRequest()`、`ModbusRtu_BuildWriteSingleRequest()`、`ModbusRtu_BuildWriteMultipleRequest()`、`ModbusRtu_ParseResponse()`），校验从站地址、CRC、异常码和帧长，主站与从站之间已完成回环单元测试。下游从站设备接入 RS485 总线后即可开启轮询。

详细寄存器表见 [Modbus RTU 从站](Software/MODBUS.md)。

## 硬件系统

硬件分为现场侧、保护与隔离、控制器侧、应用与网络侧。现场信号经过滤波、TVS、光耦或隔离收发器后进入 STM32。ESP32-S3 位于应用与网络侧，只通过 UART1 请求设备服务。

<p align="center">
  <a href="Documentation/images/arch-system.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/arch-system.webp?v=63182ca" width="100%" alt="硬件系统架构图">
  </a>
</p>

### 硬件功能

| 模块 | 配置 |
| --- | --- |
| 主控 | STM32F103ZET6，LQFP144，Cortex-M3 |
| 桥接与无线 | ESP32-S3-WROOM-1-N16R8，16MB Flash，8MB PSRAM |
| 模拟采集 | ADS1256，8 通道 24 位 ADC；ADR421 提供 2.5V 基准 |
| 温度采集 | MAX31865ATP+T，支持 PT100 和 PT1000 |
| 模拟输出 | LM358，输出 0 到 10V 电压和 4 到 20mA 电流 |
| 数字输入 | 8 路 TLP291-4 光耦隔离输入 |
| 数字输出 | ULN2803 驱动 8 路 G5Q-14 继电器 |
| 现场通信 | RS485、RS232 和 CAN，使用隔离收发器 |
| 电源与存储 | 24V 输入、TPS5430、AMS1117、TP5400 和 AT24C32D |

<details>
<summary><strong>展开查看硬件数据流和接口拓扑</strong></summary>
<br>

<p align="center">
  <a href="Documentation/images/arch-data-flow.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/arch-data-flow.webp?v=63182ca" width="100%" alt="硬件数据流图">
  </a>
</p>

<p align="center">
  <a href="Documentation/images/arch-io-topology.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/arch-io-topology.webp?v=63182ca" width="100%" alt="硬件接口拓扑图">
  </a>
</p>

</details>

### 关键硬件设计

- 模拟量输入经过串阻和电容滤波，SPI 信号串联 `22Ω`，降低反射和 EMI。
- RTD 的 `RTD_P`、`RTD_N` 和 `RTD_FORCE` 各配置双向 TVS。
- 数字输入和现场总线使用独立电源与地网络，控制器侧保持隔离。
- 继电器线圈配置续流回路，上电默认关闭全部输出。
- ADS1256 使用独立模拟电源，ADR421 基准经过运放缓冲。

## 构建

| 目标 | 工具链 | 工程入口 | 当前结果 |
| --- | --- | --- | --- |
| STM32F103ZET6 | Keil MDK | `Firmware/stm32/MDK-ARM/stm32_industrial_controller.uvprojx` | `0 Error(s), 0 Warning(s)` |
| ESP32-S3 | ESP-IDF 6.1 | `Firmware/esp32` | Build complete |
| 共享协议 | GCC | `Firmware/tests/protocol_test.c` | GitHub Actions 自动测试 |
| Modbus RTU | GCC | `Firmware/tests/modbus_test.c` | GitHub Actions 自动测试 |
| ESP32 组件 | IDF Component Manager | `Firmware/esp32/main/idf_component.yml` | MQTT 1.1.0 |
| 板间 UART | 115200 8N1 | `PA9/PA10` 与 `IO18/IO17` | 已通过实际链路验证 |

预编译固件见 [v1.0.0 Release](https://github.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/releases/tag/v1.0.0)：STM32 的 `.hex` 与 ESP32-S3 的 `.bin`，两份必须成对烧写。

构建 STM32 固件：

```text
打开 Firmware/stm32/MDK-ARM/stm32_industrial_controller.uvprojx
执行 Rebuild
执行 Download
```

构建 ESP32-S3 固件：

```powershell
cd Firmware/esp32
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

### 工程目录

```text
Firmware/
  shared/protocol/     两端共享的帧编解码和 CRC
  stm32/Core/          STM32 应用、驱动和 FreeRTOS 任务
  esp32/main/          ESP32-S3 桥接任务和 UART 初始化
Software/
  README.md            软件架构和任务设计
  PROTOCOL.md          消息字段、命令和错误码
  MODBUS.md            RS485 寄存器映射和异常响应
  NETWORK.md           WiFi、MQTT 和遥测 JSON
Documentation/
  images/              软件架构、硬件架构、PCB 和原理图
```

## 项目展示

<p align="center">
  <a href="Documentation/images/pcb-layout.webp">
    <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/pcb-layout.webp?v=63182ca" width="100%" alt="PCB 布局图">
  </a>
</p>

<details>
<summary><strong>展开查看 8 页原理图</strong></summary>
<br>

<table align="center">
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-power.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-power.webp?v=63182ca" width="380" alt="电源设计">
      </a>
      <br>
      <sub>电源</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-stm32.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-stm32.webp?v=63182ca" width="380" alt="STM32 主控">
      </a>
      <br>
      <sub>STM32 主控</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-esp32.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-esp32.webp?v=63182ca" width="380" alt="ESP32-S3">
      </a>
      <br>
      <sub>ESP32-S3</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-dio.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-dio.webp?v=63182ca" width="380" alt="光耦输入和继电器输出">
      </a>
      <br>
      <sub>光耦输入和继电器输出</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-comm.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-comm.webp?v=63182ca" width="380" alt="隔离通信">
      </a>
      <br>
      <sub>隔离通信</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-ain.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-ain.webp?v=63182ca" width="380" alt="模拟量采集">
      </a>
      <br>
      <sub>模拟量采集</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-aout.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-aout.webp?v=63182ca" width="380" alt="模拟量输出">
      </a>
      <br>
      <sub>模拟量输出</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-monitor-storage.webp">
        <img src="https://cdn.jsdelivr.net/gh/Ccmra404/stm32f103zet6-industrial-acquisition-controller@main/Documentation/images/sch-monitor-storage.webp?v=63182ca" width="380" alt="监测与存储">
      </a>
      <br>
      <sub>监测与存储</sub>
    </td>
  </tr>
</table>

</details>

## 后续方向

- 完成 OTA 固件升级和远程参数服务。
- 增加 LCD 状态页、报警页和参数配置页。
- 完成 ADS1256、MAX31865 和模拟输出的实板标定。
- 接入下游 Modbus 从站设备后启用主站轮询（帧编解码和回环测试已完成）。
- 增加看门狗复位、链路故障和控制命令的自动化联调脚本。
- 评估电池供电、功耗测量和掉电数据保护。

## 项目资料

| 文档 | 内容 |
| --- | --- |
| [软件架构](Software/README.md) | STM32、ESP32-S3、任务划分和模块接口 |
| [板间协议](Software/PROTOCOL.md) | UART 帧、消息类型、命令和错误码 |
| [Modbus RTU 从站](Software/MODBUS.md) | RS485 寄存器映射、功能码和异常响应 |
| [WiFi 与 MQTT](Software/NETWORK.md) | 网络配置、MQTT 主题和遥测 JSON |
| [Home Assistant 控制台](Software/home-assistant/README.md) | MQTT Discovery、自写 Tabler 控制台和 Mushroom Dashboard |
| [CubeMX 配置清单](Software/CUBEMX.md) | 时钟、外设、GPIO、DMA 和 FreeRTOS 配置 |
| [固件说明](Firmware/README.md) | 两套固件的构建、接线和功能范围 |
| [双板 UART 验证](Firmware/validation/README.md) | 尚硅谷 F103 与 ESP32-S3 的最小 PING/PONG 验证 |
| [协议测试](Firmware/tests/README.md) | 主机侧帧编解码、CRC 和异常路径测试 |
| [硬件设计说明](Documentation/hardware.md) | 电源、主控、模拟链路、隔离接口、温度和存储 |
| [IO 与接口规划](Documentation/io-map.md) | STM32 与 ESP32-S3 的完整引脚分配 |
| [项目术语](CONTEXT.md) | 现场侧、控制器侧、通道和模块等统一术语 |
| `Documentation/render_architecture.py` | 重新生成硬件和软件架构图 |
| `Documentation/images/` | 软件架构、硬件架构、PCB 和原理图 |
