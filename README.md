# 基于 STM32F103ZET6 与 ESP32-S3 的工业采集控制终端

<p align="center">
  <strong>STM32 负责实时采集与控制，ESP32-S3 负责桥接与远程服务扩展</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F103ZET6-1F4E79" alt="STM32F103ZET6">
  <img src="https://img.shields.io/badge/Wireless-ESP32--S3-2D7F72" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/RTOS-FreeRTOS-6E5AA8" alt="FreeRTOS">
  <img src="https://img.shields.io/badge/Framework-ESP--IDF%206.1-2D7F72" alt="ESP-IDF 6.1">
  <img src="https://img.shields.io/badge/ADC-ADS1256-C46210" alt="ADS1256">
  <img src="https://img.shields.io/badge/Protocol-CRC--16-B14E4A" alt="CRC-16 Protocol">
</p>

<p align="center">
  <a href="#项目能力">项目能力</a> |
  <a href="#软件系统">软件系统</a> |
  <a href="#通信协议">通信协议</a> |
  <a href="#硬件系统">硬件系统</a> |
  <a href="#构建与验证">构建与验证</a> |
  <a href="#项目资料">项目资料</a>
</p>

## 项目定位

这是一套面向工业采集与控制场景的双处理器终端。STM32F103ZET6 运行 FreeRTOS，承担采样、控制、报警、存储和现场总线任务。ESP32-S3 运行 ESP-IDF，负责板间桥接、状态缓存和远程服务入口。

项目不依赖单一主控完成全部工作。实时链路保留在 STM32，网络和界面任务放在 ESP32-S3。两端通过共享二进制协议通信，网络异常不会阻塞本地采样和控制。

## 项目能力

| 层面 | 当前实现 |
| --- | --- |
| 实时软件 | FreeRTOS、CMSIS-RTOS V2、5 个业务任务、ADC DMA、UART DMA 空闲接收、事件队列和继电器脉冲定时器 |
| 桥接软件 | UART1 字节流解析、HELLO、HEARTBEAT、TELEMETRY、EVENT、COMMAND、COMMAND_ACK 和 3 秒离线判断 |
| 通信协议 | `AA 55` 帧头、版本、消息类型、序号、长度、256 字节载荷和 CRC-16 |
| 状态管理 | `device_state` 统一保存设备快照，使用 mutex 保证任务读取一致性 |
| 采集控制 | 8 路 24 位模拟采集、PT100 或 PT1000 温度采集、8 路隔离数字输入、8 路继电器和 2 路模拟输出 |
| 现场通信 | RS485、RS232 和 CAN，收发器与控制侧隔离 |
| 工程实现 | STM32 与 ESP32-S3 两套固件独立构建，协议编解码代码由两端共同编译 |

已完成代码级功能：

- STM32 实时采集、控制、状态发布、事件上报和命令处理。
- ESP32-S3 遥测接收、状态缓存、心跳发送、在线判断和 ACK 解析。
- 继电器掩码、继电器脉冲、模拟输出、故障清除和配置保存命令。
- EEPROM 参数结构包含 `magic`、`version` 和 `crc`，校验失败时回退默认值。

保留的扩展入口：

- ESP32-S3 的 WiFi、MQTT、WebSocket、OTA 和远程配置。
- LCD、音频和本地人机界面。
- ADS1256、MAX31865 和现场总线的实物标定。

## 软件系统

软件由 STM32 实时固件、ESP32-S3 桥接固件和共享协议模块组成。共享协议只依赖 C 标准库，避免两端字段布局不一致。

### 软件架构

<p align="center">
  <a href="Documentation/images/sw-architecture.webp">
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sw-architecture.webp" width="100%" alt="软件系统架构图">
  </a>
</p>

### STM32 实时固件

STM32 使用 HAL、FreeRTOS 和 CMSIS-RTOS V2。任务按实时性和职责拆分：

| 任务 | 周期 | 主要工作 |
| --- | ---: | --- |
| `monitorTask` | 50 ms | 读取 24V 和 5V 电压，刷新状态灯 |
| `controlTask` | 5 ms | 数字输入消抖、继电器同步和输入变化事件 |
| `acqTask` | 100 ms | 读取 ADS1256 八通道原始值 |
| `rtdTask` | 500 ms | 读取 MAX31865 温度并更新故障状态 |
| `bridgeTask` | 10 ms 循环 | 解析 UART 命令、发送心跳、遥测和事件 |

软件层包含以下服务：

- `device_state`：保存电源、模拟输入、温度、数字输入、继电器、模拟输出和故障位图。
- `relay_output`：上电默认关闭输出，支持位图更新和脉冲控制。
- `analog_output`：控制 DAC1 和 DAC2，并同步更新状态快照。
- `config_store`：读写 EEPROM 参数，检查版本和 CRC。
- `field_comm`：发送 RS485、RS232 和 CAN 数据。
- 8 个 FreeRTOS 单次定时器：执行继电器脉冲，到期后自动撤销对应输出位。

### ESP32-S3 桥接固件

ESP32-S3 使用 ESP-IDF 6.1。UART1 使用 `IO17 TX` 和 `IO18 RX`，波特率为 `115200`。

| 任务 | 优先级 | 主要工作 |
| --- | ---: | --- |
| `uart_rx` | 8 | 读取 UART1，使用状态机逐字节解析帧 |
| `heartbeat` | 6 | 发送 HELLO 和心跳，连续 3 秒无有效帧时判断 STM32 离线 |
| `command_router` | 6 | 提供命令发送入口，后续接入远程命令队列 |

桥接层保存最近一次遥测，使用临界区保护在线状态和最后接收时间。收到 `COMMAND_ACK` 后解析请求编号、命令编号、结果和补充信息。

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

### 任务与数据流

<p align="center">
  <a href="Documentation/images/sw-task-flow.webp">
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sw-task-flow.webp" width="100%" alt="软件任务和数据流图">
  </a>
</p>

上行路径发送采集值、设备状态和事件。下行路径接收继电器、DAC 和配置命令。ESP32-S3 只发送命令，不直接操作 STM32 的 GPIO。

### 软件设计要点

- 实时域和网络域分离，网络扩展失败不影响本地闭环。
- 任务之间使用状态快照、消息队列、互斥锁和定时器。
- 遥测、事件和 ACK 使用不同消息类型，事件不会被遥测覆盖。
- 固件参数包含版本和 CRC，异常配置不会静默写入。
- UART1 使用 DMA 空闲接收，接收中断只投递字节，协议解析放在任务中。
- 共享协议代码不使用 HAL、FreeRTOS 或 ESP-IDF API，可在主机侧测试。

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
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sw-protocol-flow.webp" width="100%" alt="板间协议和命令流程图">
  </a>
</p>

完整字段和命令定义见[板间协议](Software/PROTOCOL.md)。

## 硬件系统

硬件分为现场侧、保护与隔离、控制器侧、应用与网络侧。现场信号经过滤波、TVS、光耦或隔离收发器后进入 STM32。ESP32-S3 位于应用与网络侧，只通过 UART1 请求设备服务。

<p align="center">
  <a href="Documentation/images/arch-system.webp">
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/arch-system.webp" width="100%" alt="硬件系统架构图">
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
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/arch-data-flow.webp" width="100%" alt="硬件数据流图">
  </a>
</p>

<p align="center">
  <a href="Documentation/images/arch-io-topology.webp">
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/arch-io-topology.webp" width="100%" alt="硬件接口拓扑图">
  </a>
</p>

</details>

### 关键硬件设计

- 模拟量输入经过串阻和电容滤波，SPI 信号串联 `22Ω`，降低反射和 EMI。
- RTD 的 `RTD_P`、`RTD_N` 和 `RTD_FORCE` 各配置双向 TVS。
- 数字输入和现场总线使用独立电源与地网络，控制器侧保持隔离。
- 继电器线圈配置续流回路，上电默认关闭全部输出。
- ADS1256 使用独立模拟电源，ADR421 基准经过运放缓冲。

## 构建与验证

| 目标 | 工具链 | 工程入口 | 当前结果 |
| --- | --- | --- | --- |
| STM32F103ZET6 | Keil MDK | `Firmware/stm32/MDK-ARM/stm32_industrial_controller.uvprojx` | `0 Error(s), 0 Warning(s)` |
| ESP32-S3 | ESP-IDF 6.1 | `Firmware/esp32` | Build complete |

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

当前验证范围：

- 两套固件均已通过编译。
- 共享协议包含帧构建、解析、CRC 和消息字段校验。
- ADS1256 寄存器时序、MAX31865 标定参数、继电器脉冲时间和现场总线负载仍需在实物板上验证。

### 工程目录

```text
Firmware/
  shared/protocol/     两端共享的帧编解码和 CRC
  stm32/Core/          STM32 应用、驱动和 FreeRTOS 任务
  esp32/main/          ESP32-S3 桥接任务和 UART 初始化
Software/
  README.md            软件架构和任务设计
  PROTOCOL.md          消息字段、命令和错误码
Documentation/
  images/              软件架构、硬件架构、PCB 和原理图
```

## 项目展示

<p align="center">
  <a href="Documentation/images/pcb-layout.webp">
    <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/pcb-layout.webp" width="100%" alt="PCB 布局图">
  </a>
</p>

<details>
<summary><strong>展开查看 8 页原理图</strong></summary>
<br>

<table align="center">
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-power.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-power.webp" width="380" alt="电源设计">
      </a>
      <br>
      <sub>电源</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-stm32.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-stm32.webp" width="380" alt="STM32 主控">
      </a>
      <br>
      <sub>STM32 主控</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-esp32.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-esp32.webp" width="380" alt="ESP32-S3">
      </a>
      <br>
      <sub>ESP32-S3</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-dio.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-dio.webp" width="380" alt="光耦输入和继电器输出">
      </a>
      <br>
      <sub>光耦输入和继电器输出</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-comm.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-comm.webp" width="380" alt="隔离通信">
      </a>
      <br>
      <sub>隔离通信</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-ain.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-ain.webp" width="380" alt="模拟量采集">
      </a>
      <br>
      <sub>模拟量采集</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-aout.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-aout.webp" width="380" alt="模拟量输出">
      </a>
      <br>
      <sub>模拟量输出</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-monitor-storage.webp">
        <img src="https://raw.githubusercontent.com/Ccmra404/stm32f103zet6-industrial-acquisition-controller/main/Documentation/images/sch-monitor-storage.webp" width="380" alt="监测与存储">
      </a>
      <br>
      <sub>监测与存储</sub>
    </td>
  </tr>
</table>

</details>

## 后续方向

- 接入 WiFi、MQTT、WebSocket 和 OTA，完成远程状态发布与固件升级。
- 增加 LCD 状态页、报警页和参数配置页。
- 完成 ADS1256、MAX31865 和模拟输出的实板标定。
- 增加协议测试向量、主机侧单元测试和异常链路测试。
- 评估电池供电、功耗测量和掉电数据保护。

## 项目资料

| 文档 | 内容 |
| --- | --- |
| [软件架构](Software/README.md) | STM32、ESP32-S3、任务划分和模块接口 |
| [板间协议](Software/PROTOCOL.md) | UART 帧、消息类型、命令和错误码 |
| [CubeMX 配置清单](Software/CUBEMX.md) | 时钟、外设、GPIO、DMA 和 FreeRTOS 配置 |
| [固件说明](Firmware/README.md) | 两套固件的构建、接线和功能范围 |
| [硬件设计说明](Documentation/hardware.md) | 电源、主控、模拟链路、隔离接口、温度和存储 |
| [IO 与接口规划](Documentation/io-map.md) | STM32 与 ESP32-S3 的完整引脚分配 |
| [项目术语](CONTEXT.md) | 现场侧、控制器侧、通道和模块等统一术语 |
| `Documentation/render_architecture.py` | 重新生成硬件和软件架构图 |
| `Documentation/images/` | 软件架构、硬件架构、PCB 和原理图 |
