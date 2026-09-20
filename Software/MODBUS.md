# Modbus RTU 从站

STM32 通过 USART2 和隔离 RS485 接口提供 Modbus RTU 从站。当前从站地址为 `1`，串口参数为 `115200 8N1`。

## 功能码

| 功能码 | 名称 | 行为 |
| ---: | --- | --- |
| `0x03` | Read Holding Registers | 读取 1 到 125 个保持寄存器 |
| `0x06` | Write Single Register | 写入一个可写保持寄存器 |

异常响应：

| 编码 | 含义 |
| ---: | --- |
| `0x01` | 不支持的功能码 |
| `0x02` | 寄存器地址非法或只读 |
| `0x03` | 数量或写入值非法 |

CRC 错误、广播地址和从站地址不匹配时，从站保持静默。

## 保持寄存器

PDU 地址为报文中的零基地址。

| PDU 地址 | 访问 | 内容 | 单位或转换 |
| ---: | --- | --- | --- |
| `0x0000` | 只读 | STM32 运行时间 | 秒，16 位自然回绕 |
| `0x0001` | 只读 | 24V 电源 | mV |
| `0x0002` | 只读 | 5V 电源 | mV |
| `0x0003` | 只读 | RTD 温度 | 0.1 摄氏度，有符号 16 位 |
| `0x0004` | 只读 | 数字输入位图 | bit0 到 bit7 |
| `0x0005` | 只读 | 继电器输出位图 | bit0 到 bit7 |
| `0x0006` | 只读 | 故障位图 | 与板间协议 `fault_bits` 一致 |
| `0x0010` 到 `0x001F` | 只读 | 8 路 ADS1256 原始值 | 每通道低 16 位、高 16 位 |
| `0x0020` | 读写 | 继电器输出掩码 | 低 8 位有效 |
| `0x0021` | 读写 | DAC1 原始值 | `0` 到 `4095` |
| `0x0022` | 读写 | DAC2 原始值 | `0` 到 `4095` |
| `0x0023` | 读写 | 清除锁存故障 | 故障位掩码 |

ADS1256 通道 `n` 的低 16 位位于 `0x0010 + n * 2`，高 16 位位于 `0x0011 + n * 2`。把两个寄存器组合成 32 位值后，取低 24 位并按有符号数解释。

## 接收流程

```text
USART2 byte
    |
    v
modbusTask polling
    |
    v
2 ms inter-frame gap
    |
    v
ModbusRtu_Process
    |
    +--> 0x03: read register snapshot
    |
    +--> 0x06: write register and queue output action
    |
    v
RS485 direction -> transmit
```

`modbusTask` 使用 2 ms 静默间隔判断帧结束。寄存器更新通过 `device_state` 和驱动接口完成，协议解析器本身不依赖 HAL 或 FreeRTOS。

## 主机测试

`Firmware/tests/modbus_test.c` 在主机侧验证 CRC、`0x03`、`0x06`、异常响应和静默丢弃路径。GitHub Actions 会随每次提交执行该测试。
