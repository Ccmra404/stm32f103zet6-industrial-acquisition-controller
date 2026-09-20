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
| ESP32-S3 | UART 桥接、状态缓存、命令入口、WiFi 和 MQTT 遥测 |

STM32 始终负责硬实时路径。扩展应用层通过 ESP32-S3 下发命令，ESP32-S3 不直接操作 STM32 GPIO。

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
- CAN 初始化、RS485 方向控制，以及 RS232（UART4 中断）和 CAN（FIFO0 中断）接收队列
- Modbus RTU 从站 `0x03`、`0x06`、`0x10`，以及主站请求编解码库
- HELLO、HEARTBEAT、TELEMETRY、EVENT、DIAGNOSTICS、BUS_RX、COMMAND 和 COMMAND_ACK
- 遥测包含 DAC1、DAC2 回读值，上位机可以确认模拟输出是否真的生效
- 任务活性监督、IWDG、栈余量和队列丢包诊断
- 最近 16 条命令结果缓存和重复请求去重

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
- DIAGNOSTICS 接收和运行数据查询
- BUS_RX 接收、帧计数和 `industrial/bus` MQTT 发布
- COMMAND_ACK 接收和递增应答序号转发
- STM32 在线和超时检测
- 命令队列、ACK 匹配、500 ms 超时和自动重试
- UART0 控制台命令
- WiFi Station、NVS 网络配置和 MQTT JSON 遥测
- Home Assistant MQTT Discovery、可用性主题和自动实体注册

控制台命令：

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
mqttauth <user> <password>
token <token>
tb <device-access-token>
reconnect
```

ESP-IDF 6.1 的 MQTT 组件由 `Firmware/esp32/main/idf_component.yml` 管理，首次构建会下载 `espressif/mqtt`。

Home Assistant 和 Mushroom 控制台配置见
[硬件监控 Dashboard](../Software/home-assistant/README.md)。

LCD 和音频模块属于可选外设，当前固件不初始化它们，也不在 HELLO 能力位中声明。

## 协议

协议定义见 [板间协议](../Software/PROTOCOL.md)。`Firmware/shared/protocol` 同时被 STM32 和 ESP32-S3 编译，避免两端字段布局不一致。

共享协议在主机侧运行回归测试：

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -IFirmware/shared/protocol \
  Firmware/shared/protocol/bridge_protocol.c \
  Firmware/tests/protocol_test.c \
  -o protocol_test
./protocol_test
```

测试说明见 [协议测试](tests/README.md)。GitHub Actions 会自动运行协议测试和 ESP32-S3 构建。

如果需要先验证两块开发板的 UART 连线，使用 [双开发板 UART 验证](validation/README.md)。验证工程使用 `USART1 PA9/PA10`，实际链路已经通过连续 `STM32_ALIVE` 和 `PONG` 日志验证。

## 当前验证状态

STM32 工程和 ESP32-S3 工程都已通过编译。ADS1256 寄存器、MAX31865 标定参数、继电器时序和现场总线负载仍需要在实物板上验证。
