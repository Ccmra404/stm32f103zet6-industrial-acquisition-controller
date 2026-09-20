# STM32CubeMX 配置清单

本文说明 STM32F103ZET6 固件工程的 CubeMX 配置顺序。配置目标是生成 HAL 初始化代码，再由 `App/` 模块实现业务逻辑。

## 开始前

使用本地教程工程 `09_usart_rolling_hal` 作为骨架。该工程不包含在当前仓库中。

工程目录、工程名和目标名只能使用 ASCII 字符。不要使用中文、空格或特殊符号。

以下路径会触发 ARMCC 的路径编码问题：

```text
C:\Users\zalry\Desktop\STM32工业检测
```

推荐路径：

```text
C:\Users\zalry\Desktop\stm32-esp32-industrial-acquisition-controller\Firmware\stm32\stm32_industrial_controller
```

Keil 会使用目标名创建 `Objects` 下的中间目录。目标名包含中文时，编译器无法删除或创建 `*.d` 文件。

该工程已经配置为 `STM32F103ZETx`、LQFP144、8MHz HSE、32.768kHz LSE、72MHz 主频、SWD 和 USART1。复制一份工程后修改，不要直接编辑原教程工程。

## 待确认问题

当前 IO 文档把 `PC6` 到 `PC9` 标为 ADS1256 D0 到 D3。ADS1256 没有这组并行数据脚，它只使用 SPI、`DRDY`、`CS`、`RESET` 和 `SYNC`。

在确认原理图前，`PC6` 到 `PC9` 保持未分配。需要查清以下任一情况：

- 这四根线没有连接，IO 表应删除它们。
- 这四根线连接了其他器件，IO 表和固件应改成正确名称。
- 原理图沿用了 AD7606 的并行接口，需要重新设计 ADS1256 连接。

不要把 `PC6` 到 `PC9` 配成“ADS1256 D0 到 D3”。

## 系统配置

| 项目 | 配置 |
| --- | --- |
| MCU | `STM32F103ZETx` |
| Package | `LQFP144` |
| RCC HSE | Crystal or Ceramic Resonator，8MHz |
| RCC LSE | Crystal or Ceramic Resonator，32.768kHz |
| SYS Debug | Serial Wire |
| HAL Timebase | TIM6 |
| FreeRTOS Timebase | SysTick |

选择 Serial Wire 后，`PB3` 和 `PB4` 可以用于 SPI3。JTAG 不使用。

## 时钟树

| 时钟 | 目标值 |
| --- | ---: |
| HSE | 8MHz |
| PLL Source | HSE |
| PLL Multiplier | x9 |
| SYSCLK | 72MHz |
| AHB | 72MHz |
| APB1 | 36MHz |
| APB2 | 72MHz |
| ADC Clock | 12MHz |
| RTC | LSE，32.768kHz |

APB1 定时器时钟为 72MHz。ADC 预分频使用 `/6`，不要超过 14MHz。

## 外设配置

### USART1

| 项目 | 配置 |
| --- | --- |
| Mode | Asynchronous |
| TX | `PA9` |
| RX | `PA10` |
| Baud rate | 115200 |
| Word length | 8 bits |
| Parity | None |
| Stop bits | 1 |
| DMA RX | Circular，Byte |
| Interrupt | USART1 global interrupt |

主机与 ESP32-S3 之间使用 USART1。协议解析使用 DMA 接收和 IDLE 中断，不在中断里直接解析完整帧。

教程工程默认把 `PA9` 配成 TX、`PA10` 配成 RX，与当前硬件一致。ESP32-S3 的 `IO18` 连接 STM32 的 `PA9`，ESP32-S3 的 `IO17` 连接 STM32 的 `PA10`。

### USART2

| 项目 | 配置 |
| --- | --- |
| Mode | Asynchronous |
| TX | `PA2` |
| RX | `PA3` |
| Baud rate | 115200 |
| Word length | 8 bits |
| Parity | None |
| Stop bits | 1 |

`PA1` 配置为 GPIO 输出，用作 RS485 方向控制。默认输出低电平，表示接收方向。

### USART4

| 项目 | 配置 |
| --- | --- |
| Mode | Asynchronous |
| TX | `PC10` |
| RX | `PC11` |
| Baud rate | 115200 |
| Word length | 8 bits |
| Parity | None |
| Stop bits | 1 |

### CAN1

| 项目 | 配置 |
| --- | --- |
| Mode | Normal |
| RX | `PA11` |
| TX | `PA12` |
| Bit rate | 500kbps |
| Automatic retransmission | Enable |
| RX interrupt | Enable |

先按 500kbps 配置。最终位时序根据 CAN 时钟和采样点检查后再固定。

### SPI2

SPI2 连接 ADS1256。

| 项目 | 配置 |
| --- | --- |
| Mode | Full-Duplex Master |
| SCK | `PB13` |
| MISO | `PB14` |
| MOSI | `PB15` |
| NSS | Software |
| Data size | 8 bits |
| First bit | MSB |
| Clock polarity | Low |
| Clock phase | 1 Edge |
| Prescaler | `/32` |
| Bit order | MSB First |

SPI2 时钟先使用约 1.125MHz。ADS1256 初始化完成后可以再提高，必须核对芯片手册的最大 SCLK 和时钟周期。

ADS1256 控制脚：

| 信号 | STM32 引脚 | 初始状态 |
| --- | --- | --- |
| `CS` | `PG2` | High |
| `DRDY` | `PG3` | EXTI，下降沿 |
| `RESET` | `PG4` | High |
| `SYNC` | `PG5` | High |

### SPI3

SPI3 连接 MAX31865。

| 项目 | 配置 |
| --- | --- |
| Mode | Full-Duplex Master |
| SCK | `PB3` |
| MISO | `PB4` |
| MOSI | `PB5` |
| NSS | Software |
| Data size | 8 bits |
| First bit | MSB |
| Clock polarity | Low |
| Clock phase | 1 Edge |
| Prescaler | `/16` |

MAX31865 控制脚：

| 信号 | STM32 引脚 | 初始状态 |
| --- | --- | --- |
| `CS` | `PB8` | High |
| `DRDY` | `PB9` | EXTI，下降沿 |

### I2C1

| 项目 | 配置 |
| --- | --- |
| Mode | I2C |
| SCL | `PB6` |
| SDA | `PB7` |
| Speed | 100kHz |
| Addressing | 7-bit |

I2C1 保留给外部扩展接口。

### I2C2

| 项目 | 配置 |
| --- | --- |
| Mode | I2C |
| SCL | `PB10` |
| SDA | `PB11` |
| Speed | 100kHz |
| Addressing | 7-bit |

I2C2 连接 AT24C32D。先使用 100kHz，确认总线稳定后再考虑提速。

### ADC1

| 项目 | 配置 |
| --- | --- |
| Clock prescaler | `/6` |
| Resolution | 12 bits |
| Scan conversion | Enable |
| Continuous conversion | Enable |
| DMA continuous requests | Enable |
| Number of conversions | 2 |
| Rank 1 | `ADC_IN10`，`PC0`，24V 监测 |
| Rank 2 | `ADC_IN11`，`PC1`，5V 监测 |
| Sampling time | 71.5 cycles |

ADC 节点使用 DMA circular 写入两个 `uint16_t` 样本。

### DAC

| 通道 | 引脚 | 配置 |
| --- | --- | --- |
| DAC_OUT1 | `PA4` | Output buffer enabled |
| DAC_OUT2 | `PA5` | Output buffer enabled |

输出范围和校准系数由 `App/analog_output` 管理。

### IWDG

| 项目 | 配置 |
| --- | --- |
| Clock source | LSI |
| Prescaler | 64 |
| Reload | 2500 |
| Timeout | 约 4 秒 |

IWDG 由 `monitorTask` 统一刷新。只有 `controlTask`、`acqTask`、`monitorTask`、`rtdTask` 和 `bridgeTask` 全部在期限内更新活性标记时才执行刷新。

## GPIO 配置

### 输出

| 引脚 | 功能 | 模式 | 初始状态 |
| --- | --- | --- | --- |
| `PA1` | RS485 方向 | Push-pull output | Low |
| `PA8` | 状态灯 | Push-pull output | Low |
| `PB0` | ESP32 使能 | Push-pull output | High |
| `PB12` | ESP32 唤醒 | Push-pull output | Low |
| `PD8` 到 `PD15` | 继电器 1 到 8 | Push-pull output | Low |

继电器上电必须全部关闭。不要在 CubeMX 中设置上电恢复上次输出。

### 输入

| 引脚 | 功能 | 模式 |
| --- | --- | --- |
| `PD2` | 数字输入 1 | GPIO input，pull-up |
| `PG9` 到 `PG15` | 数字输入 2 到 8 | GPIO input，pull-up |

数字输入使用 1ms 软件扫描和消抖。除调试需要外，不把每个输入都配置成 EXTI。

### 调试与启动

| 引脚 | 功能 | 模式 |
| --- | --- | --- |
| `PA13` | SWDIO | Serial Wire |
| `PA14` | SWCLK | Serial Wire |
| `PB2` | BOOT1 | Input |

`PC14` 和 `PC15` 保留给 LSE，不能改成普通 GPIO。

## DMA 与中断

| 中断或 DMA | 用途 | 约束 |
| --- | --- | --- |
| `DMA1 Channel5` | USART1 RX | Circular，Byte |
| `USART1_IRQn` | DMA IDLE 检测 | 只提交事件，不解析协议 |
| `EXTI3_IRQn` | ADS1256 DRDY | 通知采样任务 |
| `EXTI9_5_IRQn` | MAX31865 DRDY | 通知温度任务 |
| `CAN1_RX0_IRQn` | CAN 接收 | 把帧投递到队列 |
| `UART4_IRQn` | RS232 接收 | 单字节中断接收，写入环形缓冲 |
| `ADC1` DMA | 24V、5V 采样 | Circular |

FreeRTOS 使用 `NVIC_PRIORITYGROUP_4`。调用 FreeRTOS API 的中断优先级必须低于 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。不要在 EXTI 和 UART 中断里执行 SPI 事务、EEPROM 写入或 LCD 刷新。

## FreeRTOS

在 **Middleware and Software Packs** 中启用 `FREERTOS`，接口选择 `CMSIS_V2`。

初始配置：

| 项目 | 初始值 |
| --- | ---: |
| Scheduler | Preemptive |
| Tick rate | 1000Hz |
| Heap | `heap_4` |
| Total heap | 12KB，按实际堆栈测量后调整 |
| Default task | 删除或改名 |

任务清单见[软件架构](README.md)。CubeMX 只生成内核初始化和任务入口，具体业务放在 `App/`。

## 工程生成

在 **Project Manager** 中设置：

| 项目 | 配置 |
| --- | --- |
| Toolchain | MDK-ARM V5 |
| Firmware Package | 与现有 F1 工程一致 |
| Generate peripheral initialization as pair | Enable |
| Copy only necessary library files | Enable |
| Keep user code | Enable |

生成后立即执行一次编译，确认空工程的时钟、SWD 和 USART1 可以工作。再接业务模块和 FreeRTOS 任务。

## 推荐配置顺序

1. 复制现有 `09_usart_rolling_hal` 工程。
2. 配置 RCC、SYS、TIM6 和时钟树。
3. 配置 USART、SPI、I2C、CAN、ADC 和 DAC。
4. 配置 GPIO、EXTI 和 DMA。
5. 配置 IWDG。
6. 启用 FreeRTOS 和 CMSIS-V2。
7. 检查引脚冲突并生成代码。
8. 编译并下载空工程。
9. 验证 SWD、USART1 回环和 LED。
10. 再添加业务模块。

完整的固定引脚说明见 [IO 与接口规划](../Documentation/io-map.md)。
