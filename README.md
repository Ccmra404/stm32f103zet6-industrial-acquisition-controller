# 基于STM32F103ZET6与ESP32-S3的工业采集控制终端

<p align="center">
  <strong>STM32负责实时采集和控制，ESP32-S3负责显示、联网和远程交互</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F103ZET6-1F4E79" alt="STM32F103ZET6">
  <img src="https://img.shields.io/badge/Wireless-ESP32--S3-2D7F72" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/ADC-ADS1256-C46210" alt="ADS1256">
  <img src="https://img.shields.io/badge/Output-0--10V%20%2F%204--20mA-1F4E79" alt="模拟量输出">
  <img src="https://img.shields.io/badge/Bus-Isolated%20RS485-2D7F72" alt="隔离RS485">
</p>

## 项目概述

本项目是一块工业数据采集控制板，主控为 STM32F103ZET6。板载 ESP32-S3 提供显示、WiFi、MQTT 和远程交互能力。

STM32 负责采集现场信号、执行控制逻辑和驱动隔离接口。ESP32-S3 负责把数据展示出来，并把数据发送到网络。两个处理器通过 UART 通信，实时控制与网络任务互不阻塞。

## 核心能力

| 能力 | 实现 |
| --- | --- |
| 高精度采集 | ADS1256 八通道24位ADC，ADR421提供2.5V基准 |
| 标准模拟输出 | LM358实现0到10V电压输出和4到20mA电流输出 |
| 隔离数字输入 | 八路TLP291-4光耦输入 |
| 继电器输出 | ULN2803驱动八路继电器 |
| 隔离现场总线 | TD541S485H隔离RS485收发器 |
| 工业电源 | 24V输入，5V和3.3V电源域，TP5400电池路径 |
| 双MCU通信 | STM32与ESP32-S3之间使用UART连接 |
| 联网与显示 | ESP32-S3连接WiFi，预留LCD和MQTT接口 |

## 系统组成

```text
模拟量输入
    │
    ▼
RC抗混叠滤波
    │
    ▼
ADS1256 24位ADC ── SPI ──┐
                          │
光耦输入 ────────────────▶│
                          ▼
                ┌──────────────────┐
                │ STM32F103ZET6    │
                │ 采集、控制、报警  │
                └────────┬─────────┘
                         │ UART
                         ▼
                ┌──────────────────┐
                │ ESP32-S3         │
                │ 显示、WiFi、MQTT │
                └──────────────────┘
                         │
                         ▼
                    手机或云平台
```

## 硬件模块

| 模块 | 主要器件 | 作用 |
| --- | --- | --- |
| 主控 | STM32F103ZET6 | 实时采集和本地控制 |
| 联网 | ESP32-S3-WROOM-1-N16R8 | 显示、WiFi和远程通信 |
| 模拟输入 | ADS1256、ADR421 | 24位多通道采集和精密基准 |
| 模拟输出 | LM358 | 0到10V和4到20mA输出 |
| 数字输入 | TLP291-4 | 八路光耦隔离输入 |
| 数字输出 | ULN2803 | 八路继电器驱动 |
| 现场总线 | TD541S485H、ACM2520 | 隔离RS485通信 |
| 电源 | 24V输入、TPS5430、AMS1117、TP5400 | 多路供电和电池备份 |

详细电路参数见[硬件设计说明](Documentation/hardware.md)。

## 原理图

工程包含7个原理图页面。点击图片可以查看大图。

<table align="center">
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-power.webp">
        <img src="Documentation/images/sch-power.webp" width="380" alt="电源设计">
      </a>
      <br>
      <sub>电源</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-stm32.webp">
        <img src="Documentation/images/sch-stm32.webp" width="380" alt="STM32主控">
      </a>
      <br>
      <sub>STM32主控</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-esp32.webp">
        <img src="Documentation/images/sch-esp32.webp" width="380" alt="ESP32-S3">
      </a>
      <br>
      <sub>ESP32-S3</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-dio.webp">
        <img src="Documentation/images/sch-dio.webp" width="380" alt="八路光耦输入和继电器输出">
      </a>
      <br>
      <sub>八路光耦输入和继电器输出</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-comm.webp">
        <img src="Documentation/images/sch-comm.webp" width="380" alt="隔离RS485通信">
      </a>
      <br>
      <sub>隔离RS485</sub>
    </td>
    <td align="center">
      <a href="Documentation/images/sch-ain.webp">
        <img src="Documentation/images/sch-ain.webp" width="380" alt="模拟量采集">
      </a>
      <br>
      <sub>模拟量采集</sub>
    </td>
  </tr>
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-aout.webp">
        <img src="Documentation/images/sch-aout.webp" width="380" alt="模拟量输出">
      </a>
      <br>
      <sub>模拟量输出</sub>
    </td>
    <td></td>
  </tr>
</table>

## 设计要点

### 双MCU并行

STM32处理实时任务。ESP32-S3处理显示和网络任务。STM32通过UART向ESP32-S3发送采集数据，ESP32-S3向STM32下发配置和控制命令。

### 高精度模拟链路

模拟输入先经过10kΩ串阻和10nF电容滤波，再进入ADS1256。基准链路使用ADR421和运放缓冲，降低基准源负载变化对采集结果的影响。

### 工业模拟输出

模拟输出页提供0到10V电压输出和4到20mA电流输出。两种输出都使用STM32的DAC或PWM信号作为控制源。

### 隔离接口

数字输入使用TLP291-4光耦隔离。继电器输出使用ULN2803驱动。RS485接口使用TD541S485H和ACM2520，降低现场干扰对主控的影响。

### 电源分区

24V输入经过降压电路得到5V和3.3V。模拟电源和数字电源使用磁珠或0Ω电阻隔离。TP5400提供电池充放电和升压路径。

## 接口

| 接口 | 数量 | 说明 |
| --- | ---: | --- |
| 模拟输入 | 8 | ADS1256通道 |
| 模拟输出 | 2 | 0到10V和4到20mA |
| 光耦输入 | 8 | TLP291-4隔离 |
| 继电器输出 | 8 | ULN2803驱动 |
| RS485 | 1 | 隔离总线接口 |
| UART | 1 | STM32与ESP32-S3通信 |
| SWD | 1 | STM32下载和调试 |
| USB | 1 | ESP32-S3下载和调试 |

## 文档

| 文档 | 内容 |
| --- | --- |
| [硬件设计说明](Documentation/hardware.md) | 电源、主控、模拟链路、隔离接口和通信电路 |
| `Documentation/images/` | 7页原理图导出图片 |

## 后续计划

- 增加隔离RS232接口。
- 增加隔离CAN接口。
- 增加RS485、RS232和CAN的统一通信管理层。
- 增加采集校准和输出校准流程。
- 增加Web看板、历史曲线和报警记录。
- 增加ESP32-S3 OTA升级。
- 增加连续运行、断电恢复和网络恢复测试。
