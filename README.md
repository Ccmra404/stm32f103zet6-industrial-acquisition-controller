# 基于STM32F103ZET6与ESP32-S3的工业采集控制终端

<p align="center">
  <strong>双MCU并行架构 · 高精度模拟量采集 · 标准工业模拟输出 · 隔离通信与隔离IO</strong>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/MCU-STM32F103ZET6-1f4e79" alt="STM32F103ZET6">
  <img src="https://img.shields.io/badge/Wireless-ESP32--S3-2d7f72" alt="ESP32-S3">
  <img src="https://img.shields.io/badge/ADC-ADS1256-c46210" alt="ADS1256">
  <img src="https://img.shields.io/badge/Output-0--10V%20%2F%204--20mA-1f4e79" alt="Analog Output">
  <img src="https://img.shields.io/badge/Bus-Isolated%20RS485-2d7f72" alt="RS485">
</p>

<p align="center">
  <a href="#项目简介">项目简介</a> ·
  <a href="#项目速览">项目速览</a> ·
  <a href="#项目功能">项目功能</a> ·
  <a href="#核心亮点">核心亮点</a> ·
  <a href="#系统结构">系统结构</a> ·
  <a href="#硬件规格">硬件规格</a> ·
  <a href="#硬件设计图">硬件设计图</a> ·
  <a href="#硬件设计说明">硬件设计说明</a> ·
  <a href="#后续优化方向">后续优化方向</a>
</p>

## 项目简介

技术栈：`STM32F103ZET6` · `ESP32-S3` · `ADS1256` · `ADR421` · `LM358` · `RS485` · `FreeRTOS` · `立创EDA`

本项目是一个面向工业现场的数据采集与控制终端。系统采用 STM32F103ZET6 与 ESP32-S3 双MCU并行架构，STM32负责现场数据采集、隔离IO、报警判断和本地控制，ESP32-S3负责显示、WiFi联网、MQTT上传和远程交互。

硬件设计包含工业电源、主控最小系统、ESP32-S3、八路光耦输入与八路继电器输出、隔离RS485通信、ADS1256高精度模拟量采集，以及0到10V和4到20mA模拟量输出。

项目通过双MCU分工将实时控制与联网显示解耦，STM32侧可以独立完成采集、判断和执行，ESP32-S3侧可以专注显示、网络和远程交互。

## 项目速览

| 项目 | 设计内容 |
| --- | --- |
| 主控架构 | STM32F103ZET6实时采集 + ESP32-S3联网显示 |
| 模拟输入 | ADS1256八通道24位ADC，ADR421精密基准 |
| 模拟输出 | 0到10V电压输出、4到20mA电流输出 |
| 数字隔离 | TLP291-4八路光耦输入 |
| 继电器输出 | ULN2803驱动八路继电器 |
| 现场通信 | TD541S485H隔离RS485接口 |
| 工业电源 | 24V输入、5V/3.3V电源域、TP5400电池路径 |
| 设计资料 | 7页原理图导出图与完整硬件设计说明 |

## 项目功能

- **STM32F103ZET6主控系统**：负责实时采集、处理和控制。
- **ESP32-S3联网系统**：负责显示、WiFi、MQTT和远程交互。
- **八路24位模拟量采集**：使用ADS1256实现多通道高精度采集。
- **0到10V模拟量输出**：通过LM358和外围电路实现电压输出。
- **4到20mA模拟量输出**：通过LM358和驱动电路实现电流环输出。
- **八路光耦输入**：使用TLP291-4进行现场侧与MCU侧隔离。
- **八路继电器输出**：提供工业负载控制接口。
- **隔离RS485通信**：使用TD541S485H和隔离电源构建现场总线接口。
- **工业电源输入**：支持24V输入、5V中间电源、3.3V数字电源和电池输入。
- **板间通信**：STM32与ESP32-S3之间提供UART业务链路。

## 核心亮点

### 双MCU并行架构

系统将实时控制与联网显示拆分成两条并行链路：

```text
STM32实时链路
模拟量采集 → 数据处理 → 阈值判断 → 隔离IO/继电器

ESP32-S3联网链路
UART接收 → 数据显示 → WiFi/MQTT → Web或手机端
```

网络任务不会阻塞STM32的采集和控制，STM32也可以独立完成本地报警。

### 高精度模拟量采集

模拟前端使用ADS1256作为24位ADC，输入通道带RC低通滤波。基准电压由ADR421产生2.5V参考，并通过精密运放跟随器缓冲，降低基准源负载变化对采集精度的影响。

### 标准工业模拟输出

模拟输出页包含两种常见工业信号：

```text
0-3.3V控制信号 → 0-10V电压输出
0-3.3V控制信号 → 4-20mA电流输出
```

这两路输出便于连接变频器、比例阀、仪表和PLC模拟输入。

### 光耦隔离输入和继电器输出

现场输入使用TLP291-4光耦隔离，输出使用ULN2803驱动八路继电器。输入端和MCU端电气隔离，降低现场干扰和地电位差对主控的影响。

### 隔离RS485

通信电路使用TD541S485H隔离收发器、ACM2520共模电感和SM712瞬态抑制器件，提高总线的抗干扰能力和现场可靠性。

### 电源冗余与电池输入

电源部分包含24V输入、降压电路、5V和3.3V电源域，并提供TP5400电池充放电和升压路径，用于掉电保护和便携供电。

## 系统结构

```text
                    ┌────────────────────────┐
模拟量输入 ─滤波──▶│ ADS1256 24位ADC         │
                    └───────────┬────────────┘
                                │ SPI
                                ▼
                    ┌────────────────────────┐
光耦输入  ─────────▶│ STM32F103ZET6          │
继电器输出◀─────────│ 采集、控制、报警、协议   │
                    └───────────┬────────────┘
                                │ UART
                                ▼
                    ┌────────────────────────┐
                    │ ESP32-S3               │
                    │ 显示、WiFi、MQTT、Web   │
                    └───────────┬────────────┘
                                │
                                ▼
                        手机或云平台
```

## 硬件规格

| 项目 | 参数 |
| --- | --- |
| 主控MCU | STM32F103ZET6，LQFP144 |
| 网络MCU | ESP32-S3-WROOM-1-N16R8 |
| 模拟采集 | ADS1256，多通道24位ADC |
| 模拟基准 | ADR421，2.5V |
| 模拟输出 | LM358，0到10V、4到20mA |
| 数字隔离 | TLP291-4光耦 |
| 继电器驱动 | ULN2803 |
| 现场通信 | 隔离RS485 |
| 隔离电源 | ACM2520系列隔离电源 |
| 电源输入 | 24V工业电源 |
| 电池管理 | TP5400 |
| 调试接口 | SWD和UART |
| 设计工具 | 立创EDA |

## 硬件设计图

<table align="center">
  <tr>
    <td align="center">
      <a href="Documentation/images/sch-power.webp">
        <img src="Documentation/images/sch-power.webp" width="380" alt="电源设计">
      </a>
      <br>
      <sub>电源设计</sub>
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
      <sub>隔离RS485通信</sub>
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

## 硬件设计说明

### 1. 电源设计

电源部分包含24V输入保护、降压电路、5V和3.3V电源、电池充放电和升压路径。模拟电源与数字电源使用磁珠或0Ω电阻进行隔离。

### 2. STM32主控

STM32F103ZET6主控电路包含：

- 8MHz主晶振和32.768kHz RTC晶振
- 复位电路和BOOT配置
- SWD下载接口
- 独立VDDA滤波
- VDD和VSS逐脚去耦
- ESP32-S3 UART接口
- ADC和DAC接口

### 3. ESP32-S3

ESP32-S3部分包含：

- ESP32-S3-WROOM-1-N16R8模组
- CH340K USB转串口
- Type-C下载接口
- BOOT和复位按键
- ST7789 LCD排线接口
- UART业务通信接口
- WiFi和外部扩展接口

### 4. 模拟量采集

```text
模拟输入
    ↓
10kΩ串阻
    ↓
10nF滤波电容
    ↓
ADS1256 AIN0~AIN7
```

ADS1256使用7.68MHz晶振，模拟电源和数字电源分别供电。基准链路使用ADR421和运放缓冲。

### 5. 模拟量输出

模拟量输出分为两种接口：

- 0到10V电压输出
- 4到20mA电流输出

两种输出均由STM32的DAC或PWM信号经过LM358和外围反馈网络转换。

### 6. 光耦输入和继电器输出

输入使用TLP291-4隔离，输出使用ULN2803驱动，并配置继电器、续流保护、指示灯和端子接口。

### 7. 隔离RS485

RS485部分包含：

- TD541S485H隔离收发器
- ACM2520共模电感
- SM712瞬态抑制
- 总线端接和保护电路
- 三端接线端子

## 仓库内容

```text
README.md
Documentation/
└── images/
    ├── sch-power.webp
    ├── sch-stm32.webp
    ├── sch-esp32.webp
    ├── sch-dio.webp
    ├── sch-comm.webp
    ├── sch-ain.webp
    └── sch-aout.webp
```

本仓库只提供项目说明和原理图导出图片，不提供可编辑工程源文件。

## 后续优化方向

| 方向 | 计划内容 |
| --- | --- |
| 采集精度 | 零点校准、增益校准和温漂测试 |
| 输出精度 | 校准0到10V和4到20mA输出 |
| 通信协议 | 增加Modbus RTU、CAN或自定义可靠帧协议 |
| 边缘预警 | 增加滑动平均、趋势分析和异常检测 |
| Web看板 | 增加实时曲线、报警记录和历史查询 |
| 远程升级 | 增加ESP32-S3 OTA和参数配置 |
| 长期稳定性 | 增加连续运行、断电恢复和网络恢复测试 |

## 参考资料

- STM32F103ZET6参考手册
- ADS1256数据手册
- ADR421数据手册
- LM358数据手册
- TD541S485H数据手册
- TP5400数据手册
