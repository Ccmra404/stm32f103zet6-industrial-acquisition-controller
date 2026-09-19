# IO 与接口规划

本文记录当前硬件设计的最终引脚分配。符号说明：

- `MCU` 表示 STM32F103ZET6。
- `ESP` 表示 ESP32-S3-WROOM-1-N16R8。
- `现场侧` 表示连接外部传感器、执行器或现场总线的网络。
- `控制器侧` 表示连接 MCU、ESP32-S3 和板载器件的网络。

## STM32 接口

| 功能 | 引脚 | 接口 | 说明 |
| --- | --- | --- | --- |
| RS485 方向 | PA1 | GPIO | 控制收发方向 |
| RS485 发送 | PA2 | USART2_TX | 连接 TD541S485H |
| RS485 接收 | PA3 | USART2_RX | 连接 TD541S485H |
| DAC1 | PA4 | DAC_OUT1 | 模拟量输出 |
| DAC2 | PA5 | DAC_OUT2 | 模拟量输出 |
| 状态灯 | PA8 | GPIO | 板载状态指示 |
| ESP32 发送 | PA9 | USART1_TX | 发送到 ESP32 UART |
| ESP32 接收 | PA10 | USART1_RX | 接收 ESP32 UART |
| CAN 接收 | PA11 | CAN1_RX | 连接 TDH541SCANFD |
| CAN 发送 | PA12 | CAN1_TX | 连接 TDH541SCANFD |
| SWDIO | PA13 | SWDIO | 调试口 |
| SWCLK | PA14 | SWCLK | 调试口 |
| ESP32 使能 | PB0 | GPIO | 控制 ESP_EN |
| BOOT1 | PB2 | BOOT1 | 启动配置 |
| MAX31865 时钟 | PB3 | SPI3_SCK | 关闭 JTAG 后使用 |
| MAX31865 输入 | PB4 | SPI3_MISO | 关闭 JTAG 后使用 |
| MAX31865 输出 | PB5 | SPI3_MOSI | 连接 MAX31865 SDI |
| I2C1 时钟 | PB6 | I2C1_SCL | 外部 I2C 接口 |
| I2C1 数据 | PB7 | I2C1_SDA | 外部 I2C 接口 |
| MAX31865 片选 | PB8 | GPIO | 低电平有效 |
| MAX31865 就绪 | PB9 | EXTI | 数据就绪中断 |
| I2C2 时钟 | PB10 | I2C2_SCL | AT24C32D |
| I2C2 数据 | PB11 | I2C2_SDA | AT24C32D |
| ESP32 唤醒 | PB12 | GPIO | 唤醒 ESP32-S3 |
| ADS1256 时钟 | PB13 | SPI2_SCK | ADS1256 时钟 |
| ADS1256 输出 | PB14 | SPI2_MISO | ADS1256 DOUT |
| ADS1256 输入 | PB15 | SPI2_MOSI | ADS1256 DIN |
| 24V 监测 | PC0 | ADC12_IN10 | 100kΩ 与 10kΩ 分压 |
| 5V 监测 | PC1 | ADC12_IN11 | 10kΩ 与 10kΩ 分压 |
| ADS1256 D0 | PC6 | 待确认 | ADS1256 没有 D0，配置前核对原理图 |
| ADS1256 D1 | PC7 | 待确认 | ADS1256 没有 D1，配置前核对原理图 |
| ADS1256 D2 | PC8 | 待确认 | ADS1256 没有 D2，配置前核对原理图 |
| ADS1256 D3 | PC9 | 待确认 | ADS1256 没有 D3，配置前核对原理图 |
| RS232 发送 | PC10 | USART4_TX | 连接 TDH541S232H |
| RS232 接收 | PC11 | USART4_RX | 连接 TDH541S232H |
| 数字输入 1 | PD2 | GPIO | 光耦输入 |
| 继电器 1 | PD8 | GPIO | ULN2803 输入 |
| 继电器 2 | PD9 | GPIO | ULN2803 输入 |
| 继电器 3 | PD10 | GPIO | ULN2803 输入 |
| 继电器 4 | PD11 | GPIO | ULN2803 输入 |
| 继电器 5 | PD12 | GPIO | ULN2803 输入 |
| 继电器 6 | PD13 | GPIO | ULN2803 输入 |
| 继电器 7 | PD14 | GPIO | ULN2803 输入 |
| 继电器 8 | PD15 | GPIO | ULN2803 输入 |
| ADS1256 CS | PG2 | GPIO | 片选 |
| ADS1256 DRDY | PG3 | EXTI | 数据就绪 |
| ADS1256 RESET | PG4 | GPIO | 复位 |
| ADS1256 SYNC | PG5 | GPIO | 同步 |
| 数字输入 2 | PG9 | GPIO | 光耦输入 |
| 数字输入 3 | PG10 | GPIO | 光耦输入 |
| 数字输入 4 | PG11 | GPIO | 光耦输入 |
| 数字输入 5 | PG12 | GPIO | 光耦输入 |
| 数字输入 6 | PG13 | GPIO | 光耦输入 |
| 数字输入 7 | PG14 | GPIO | 光耦输入 |
| 数字输入 8 | PG15 | GPIO | 光耦输入 |

### ADS1256 待确认引脚

ADS1256 使用 SPI 访问，不提供 `D0` 到 `D3` 并行数据线。`PC6` 到 `PC9` 可能来自其他 ADC 的接口定义、未连接网络或另一组控制信号。

在原理图确认前，`PC6` 到 `PC9` 保持未分配。不要把它们配置为 ADS1256 数据线。

## ESP32-S3 接口

| 功能 | 引脚 | 接口 | 说明 |
| --- | --- | --- | --- |
| BOOT | IO0 | GPIO | BOOT 按键 |
| I2C 时钟 | IO4 | I2C | ESP_I2C_SCL |
| I2C 数据 | IO5 | I2C | ESP_I2C_SDA |
| I2S MCLK | IO6 | I2S | ESP_I2S_MCLK |
| LCD CS | IO10 | SPI | ESP_LCD_CS |
| I2S DOUT | IO11 | I2S | ESP_I2S_DOUT |
| I2S LRCK | IO12 | I2S | ESP_I2S_LRCK |
| I2S DIN | IO13 | I2S | ESP_I2S_DIN |
| I2S BCLK | IO14 | I2S | ESP_I2S_BCLK |
| PA_EN | IO15 | GPIO | 为其他功放模块保留 |
| STM32 唤醒 | IO16 | GPIO | MCU_ESP32_WAKE |
| STM32 发送 | IO17 | UART1_TX | MCU_ESP32_UART_TX |
| STM32 接收 | IO18 | UART1_RX | MCU_ESP32_UART_RX |
| USB D- | IO19 | USB | ESP_USB_DM |
| USB D+ | IO20 | USB | ESP_USB_DP |
| LCD DC | IO39 | SPI | ESP_LCD_DC |
| LCD MOSI | IO40 | SPI | ESP_LCD_MOSI |
| LCD SCK | IO41 | SPI | ESP_LCD_SCK |
| LCD BL | IO42 | GPIO | ESP_LCD_BL |
| UART0 TX | IO43 | UART0 | ESP_UART0_TX |
| UART0 RX | IO44 | UART0 | ESP_UART0_RX |
| LCD RESET | IO47 | GPIO | ESP_LCD_RESET |
| RGB 或状态灯 | IO48 | GPIO | 预留 |

## ESP32-S3 保留引脚

| 引脚 | 用途 | 约束 |
| --- | --- | --- |
| IO3 | JTAG 信号源绑带 | 启动电平必须稳定 |
| IO35 | Octal PSRAM | N16R8 内部使用 |
| IO36 | Octal PSRAM | N16R8 内部使用 |
| IO37 | Octal PSRAM | N16R8 内部使用 |
| IO45 | VDD_SPI 绑带 | 保持默认电平 |
| IO46 | 启动模式绑带 | 只做测试点或保持默认电平 |

## 模块接口

| 接口 | 提供方 | 使用方 | 连接 |
| --- | --- | --- | --- |
| 模拟采集 | ADS1256 | STM32 | SPI2 |
| 温度采集 | MAX31865 | STM32 | SPI3 |
| 参数存储 | AT24C32D | STM32 | I2C2 |
| 外部 I2C | 板端接口 | 外部模块 | I2C1 |
| 板间通信 | STM32 | ESP32-S3 | UART1 |
| 显示 | ESP32-S3 | LCD | SPI |
| 音频 | ESP32-S3 | ES8311 与 NS4150B | I2C 与 I2S |
| 电源监测 | 24V 与 5V 分压 | STM32 | ADC |

## 预留扩展

| 区域 | 引脚 | 建议用途 |
| --- | --- | --- |
| PA6、PA7 | ADC、定时器或 SPI1 | 传感器扩展 |
| PE7 到 PE15 | GPIO、定时器或 FSMC | 并行接口或外部逻辑 |
| PF0 到 PF15 | GPIO 或 FSMC | 扩展接口 |
| PD3 到 PD7 | 定时器、USART2 复用或 GPIO | 编码器、PWM 或脉冲输入 |
| PD0、PD1 | CAN 重映射或 FSMC | CAN 备用 |
| IO1、IO2、IO7、IO8、IO9、IO21、IO38 | ADC、Touch 或 GPIO | ESP32-S3 扩展 |

## 设计约束

- `PA13` 和 `PA14` 保留 SWD。不要复用。
- `PB3` 和 `PB4` 使用 SPI3 时，关闭 JTAG 并保留 SWD。
- `PA11` 和 `PA12` 用于 CAN。STM32 USB 不使用。
- `PC14` 和 `PC15` 保留 32.768kHz 晶振。
- `IO35`、`IO36` 和 `IO37` 被 N16R8 内部 PSRAM 使用。
- `IO45` 和 `IO46` 不连接高负载或可变电平器件。
- `IO19` 和 `IO20` 保留 USB。
- `IO43` 和 `IO44` 保留 UART0。
