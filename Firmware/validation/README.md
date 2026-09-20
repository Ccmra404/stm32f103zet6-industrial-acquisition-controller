# 双开发板 UART 验证

这套验证固件只验证尚硅谷 STM32F103ZET6 开发板和 ESP32-S3 开发板之间的 UART 通信，不启动工业控制业务。

## 接线

两块板分别供电，只连接信号和地：

| STM32 尚硅谷板 | ESP32-S3 | 说明 |
| --- | --- | --- |
| `PA9 USART1_TX` | `GPIO18 RX` | STM32 发送到 ESP32 |
| `PA10 USART1_RX` | `GPIO17 TX` | ESP32 发送到 STM32 |
| `GND` | `GND` | 公共地 |

不连接两块板的 `3.3V` 或 `5V`。尚硅谷板的 `CN1` 八针接口中，
`CN1-4` 为 `PA10（USART1_RX）`，`CN1-6` 为 `PA9（USART1_TX）`，
`CN1-2` 或 `CN1-7` 为 `GND`。使用板载串口工具时不要让同一组
USART1 引脚被两个上位机同时占用。

## STM32 固件

烧录：

```text
Firmware/stm32/MDK-ARM/stm32_uart1_validation.uvprojx
```

串口配置固定为 `115200 8N1`。

STM32 每秒发送：

```text
STM32_ALIVE
```

收到 `PING` 后返回：

```text
PONG
```

## ESP32-S3 固件

使用 ESP-IDF 构建：

```powershell
cd Firmware/validation/esp32_uart_ping
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

如果 Windows 构建目录路径过长，改用短构建目录：

```powershell
idf.py -B C:\espbuild\uart_ping build
```

ESP32-S3 每秒发送：

```text
PING
```

STM32 返回：

```text
PONG
```

ESP32-S3 日志同时显示连续 `STM32_ALIVE` 和 `PONG` 时，说明双向 UART 链路已经接通。
