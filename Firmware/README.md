# 固件说明

本目录包含 STM32 实时控制固件、ESP32-S3 桥接固件和两端共享的二进制通信协议。

```text
Firmware/
  shared/protocol/   共享的帧编解码和 CRC
  stm32/             STM32F103ZET6 固件
  esp32/             ESP32-S3 固件
```

## 处理器分工

| 处理器 | 职责 |
| --- | --- |
| STM32F103ZET6 | ADS1256、MAX31865、数字输入、继电器、DAC、EEPROM、CAN、RS485 和 RS232 |
| ESP32-S3 | UART 桥接、状态缓存、命令路由、WiFi 和后续网络服务 |

STM32 始终负责硬实时路径。ESP32-S3 通过网络、桥接任务或后续界面下发命令，不能直接操作 STM32 GPIO。

## 板间连接

| STM32 | ESP32-S3 | 说明 |
| --- | --- | --- |
| `PA9` | `IO18` | STM32 TX 到 ESP32 RX |
| `PA10` | `IO17` | ESP32 TX 到 STM32 RX |
| `GND` | `GND` | 公共地 |

UART 使用 `115200 8N1`。所有多字节字段使用小端序。

## STM32 构建

打开 Keil 工程：

```text
Firmware/stm32/MDK-ARM/stm32_industrial_controller.uvprojx
```

执行：

```text
Rebuild
Download
```

当前 STM32 固件包含：

- CMSIS-V2 和 FreeRTOS
- ADC1 DMA 电源监测
- USART1 DMA 空闲接收
- ADS1256 八通道采集
- MAX31865 温度采集
- 八路数字输入消抖
- 八路继电器控制和脉冲输出
- DAC1、DAC2 模拟输出
- AT24C32D 配置存储
- CAN 初始化和 RS485 方向控制
- HELLO、HEARTBEAT、TELEMETRY、EVENT、COMMAND 和 COMMAND_ACK

## ESP32-S3 构建

使用 ESP-IDF 6.1：

```powershell
cd Firmware/esp32
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

工程默认配置：

```text
Flash     16MB
PSRAM     Octal 80MHz
Console   UART0
Bridge    UART1
```

ESP32-S3 当前固件包含：

- UART1 收发
- HELLO 和 HEARTBEAT
- TELEMETRY 接收和状态缓存
- COMMAND_ACK 接收
- STM32 在线和超时检测
- 桥接任务、心跳任务和命令路由任务入口

LCD 和音频模块属于可选外设，当前固件不初始化它们，也不在 HELLO 能力位中声明。

## 协议

协议定义见 [板间协议](../Software/PROTOCOL.md)。`Firmware/shared/protocol` 同时被 STM32 和 ESP32-S3 编译，避免两端字段布局不一致。

## 当前验证状态

STM32 工程和 ESP32-S3 工程都已通过编译。ADS1256 寄存器、MAX31865 标定参数、继电器时序和现场总线负载仍需要在实物板上验证。
