# 硬件设计说明

本文说明各硬件模块的实现。原理图页面位于 `Documentation/images/`，最终引脚分配见 [IO 与接口规划](io-map.md)。

## 系统参数

| 项目 | 参数 |
| --- | --- |
| 主控 MCU | STM32F103ZET6 |
| 联网 MCU | ESP32-S3-WROOM-1-N16R8 |
| 模拟输入 | ADS1256，八通道 24 位 |
| 模拟基准 | ADR421，2.5V |
| 温度输入 | MAX31865ATP+T |
| 参数存储 | AT24C32D-SSHM-T |
| 模拟输出 | LM358 |
| 数字输入隔离 | TLP291-4 |
| 继电器驱动 | ULN2803 |
| RS485 收发器 | TD541S485H |
| RS232 收发器 | TDH541S232H |
| CAN 收发器 | TDH541SCANFD |
| 电源输入 | 24V |
| 电池管理 | TP5400 |

## 电源

电源页面包含以下路径：

```text
24V 输入
  ├── 降压至 5V
  ├── 降压至 3.3V
  └── 电池充放电和升压
```

24V 输入端配置反接保护和浪涌保护。5V 用于继电器、隔离器和模拟电源。3.3V 用于 STM32、ESP32-S3、数字接口和逻辑电路。

模拟电源和数字电源使用磁珠或 0Ω 电阻隔离。ADC 基准和模拟前端使用独立的模拟电源网络。

## STM32 主控

STM32F103ZET6 使用 LQFP144 封装。主控页面包含：

- 8MHz 主晶振。
- 32.768kHz RTC 晶振。
- 复位和 BOOT 配置。
- SWD 下载接口。
- 独立 VDDA 滤波。
- 每个 VDD 引脚的去耦电容。
- ADS1256 的 SPI 接口。
- MAX31865 的 SPI 接口。
- ESP32-S3 的 UART 接口。
- 模拟量输出控制接口。
- EEPROM 的 I2C2 接口。

STM32 引脚按物理封装和接口类别分组。继电器集中在 `PD8` 到 `PD15`，数字输入集中在 `PD2` 和 `PG9` 到 `PG15`。ADS1256 的 SPI 与控制线集中在右侧。

## ESP32-S3

ESP32-S3 页面包含：

- ESP32-S3-WROOM-1-N16R8 模组。
- CH340K USB 转串口。
- Type-C 下载接口。
- BOOT 和复位按键。
- ST7789 兼容 LCD 接口。
- 与 STM32 连接的 UART 接口。
- 1×9 音频模块接口。
- WiFi 天线净空区。

ESP32-S3 负责显示、WiFi、MQTT、音频接口和远程交互。网络任务不会占用 STM32 的实时控制时间。

### LCD 接口

LCD 使用 SPI 接口。板端网络如下：

| LCD 信号 | ESP32-S3 引脚 |
| --- | --- |
| SCK | IO41 |
| MOSI | IO40 |
| DC | IO39 |
| CS | IO10 |
| RESET | IO47 |
| BL | IO42 |
| GND | GND |
| VCC | 3V3_ESP |

### 音频模块接口

`J22` 使用 1×9、2.54mm 插座，脚序与 ES8311 与 NS4150B 模块一致。

| 引脚 | 模块信号 | ESP32-S3 网络 |
| ---: | --- | --- |
| 1 | GND | GND |
| 2 | 5V | SYS_5V |
| 3 | DIN | ESP_I2S_DOUT |
| 4 | LRCK | ESP_I2S_LRCK |
| 5 | DOUT | ESP_I2S_DIN |
| 6 | SCLK | ESP_I2S_BCLK |
| 7 | MCLK | ESP_I2S_MCLK |
| 8 | SCL | ESP_I2C_SCL |
| 9 | SDA | ESP_I2C_SDA |

`ESP_I2S_DOUT` 是 ESP32-S3 输出到 Codec 的数据。`ESP_I2S_DIN` 是 Codec 返回给 ESP32-S3 的数据。当前模块的功放控制由模块内部电阻拉高，`J22` 不包含 PA_EN。

## 隔离数字输入

数字输入使用 TLP291-4 四通道光耦。每路输入包含限流电阻、输入指示和滤波。

输入信号经过光耦后进入 STM32 GPIO。现场侧和 MCU 侧使用独立电源和地网络，减少地电位差和现场干扰。

## 继电器输出

继电器驱动使用 ULN2803。每路输出包含：

- 驱动输入电阻。
- 继电器线圈。
- 续流二极管。
- 状态指示灯。
- 输出端子。

STM32 通过 ULN2803 控制继电器。输出端子和线圈电源位于现场侧。

## 模拟量采集

模拟输入链路：

```text
端子
  │
  ▼
10kΩ 串阻
  │
  ├── 10nF 到地
  │
  ▼
ADS1256 AIN0 到 AIN7
```

ADS1256 为 24 位 ADC，使用 7.68MHz 晶振。ADC 的模拟电源和数字电源分别供电。

基准链路：

```text
A_5V
  │
  ▼
ADR421
  │
  ▼
2.5V
  │
  ▼
运放缓冲
  │
  ▼
ADS1256 VREF+
```

SPI 信号串联 22Ω 电阻，用于降低反射和 EMI。

## 温度采集

MAX31865 通过 SPI3 连接 STM32。现场端包含三个 `SMBJ5.0CA` 双向 TVS，分别保护 `PT_RTD_P`、`PT_RTD_N` 和 `PT_RTD_FORCE`。

`C95` 为 `100nF` 差分滤波电容，连接在 `PT_RTD_P` 与 `PT_RTD_N` 之间。`C93` 和 `C94` 为 MAX31865 电源去耦电容。

三线 PT100 连接方式：

| 模块端子 | 网络 |
| --- | --- |
| RTD+ | PT_RTD_P |
| RTD- | PT_RTD_N |
| FORCE | PT_RTD_FORCE |

## 参数存储

板载 AT24C32D 连接 STM32 的 I2C2：

| EEPROM 引脚 | 连接 |
| --- | --- |
| SDA | PB11 |
| SCL | PB10 |
| A0、A1、A2 | GND |
| WP | GND |
| VCC | 3V3_M |
| GND | GND |

器件地址为 `0x50`。I2C2 使用两个 4.7kΩ 上拉电阻。外部 I2C 接口保留在 I2C1，两条总线不共享。

## 电源监测

电源监测页面包含两路分压：

| 监测对象 | 分压 | STM32 引脚 | 正常电压 |
| --- | --- | --- | --- |
| 24V | 100kΩ 与 10kΩ | PC0 | 约 2.18V |
| 5V | 10kΩ 与 10kΩ | PC1 | 约 2.5V |

两个 ADC 节点各有一个 100nF 到地。固件可轮询采样值并判断过压、欠压和掉电。

## 模拟量输出

模拟输出页面提供两种接口：

| 输出 | 电路 | 用途 |
| --- | --- | --- |
| 0 到 10V | LM358 电压放大 | 连接 PLC、变频器和模拟输入设备 |
| 4 到 20mA | LM358 电流环 | 连接仪表、比例阀和工业执行器 |

STM32 提供 DAC 或 PWM 控制信号。输出电路负责电平转换、电流驱动和保护。

## 隔离通信

通信页面包含三路隔离接口：

| 接口 | 收发器 | 现场信号 |
| --- | --- | --- |
| RS485 | TD541S485H | RS485_A、RS485_B、RS485_GND |
| RS232 | TDH541S232H | RS232_TX、RS232_RX、RS232_GND |
| CAN | TDH541SCANFD | CAN_H、CAN_L、CAN_GND |

RS485 和 CAN 接口包含共模电感、瞬态抑制和总线端接。隔离电源为总线侧供电。

## 板间通信

STM32 与 ESP32-S3 使用 UART 通信。STM32 发送采集数据、报警状态和设备状态。ESP32-S3 发送配置、控制命令和网络状态。

通信层使用固定帧头、长度、序号和 CRC 校验。UART 数据流采用二进制帧，不使用文本日志作为控制协议。

## 原理图页面

| 文件 | 内容 |
| --- | --- |
| `sch-power.webp` | 电源 |
| `sch-stm32.webp` | STM32 主控 |
| `sch-esp32.webp` | ESP32-S3 |
| `sch-dio.webp` | 光耦输入和继电器输出 |
| `sch-comm.webp` | RS485、RS232 和 CAN |
| `sch-ain.webp` | 模拟量采集 |
| `sch-aout.webp` | 模拟量输出 |
| `sch-monitor-storage.webp` | 温度、参数存储和电源监测 |
