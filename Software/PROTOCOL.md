# 板间协议

本文定义 STM32 与 ESP32-S3 之间的 UART1 二进制协议。所有多字节字段使用小端序。

## 物理层

| 项目 | 配置 |
| --- | --- |
| UART | 115200 baud，8 数据位，无校验，1 停止位 |
| 流控 | 无 |
| STM32 | UART1，`PA9` 发送，`PA10` 接收 |
| ESP32-S3 | UART1，`IO17` 发送，`IO18` 接收 |
| 最大载荷 | 256 字节 |
| 最大帧长 | 266 字节 |

STM32 与 ESP32-S3 的 TX 和 RX 必须交叉连接，双方共地。

## 帧格式

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 2 | `SOF` | 固定为 `AA 55` |
| 2 | 1 | `version` | 当前为 `0x01` |
| 3 | 1 | `type` | 消息类型 |
| 4 | 2 | `sequence` | 发送序号，循环递增 |
| 6 | 2 | `length` | 载荷长度，0 到 256 |
| 8 | `length` | `payload` | 消息载荷 |
| 8 + `length` | 2 | `crc16` | CRC-16/CCITT-FALSE |

CRC 覆盖 `version` 到 `payload` 的连续字节。CRC 不覆盖 `SOF`。

CRC 参数：

| 参数 | 值 |
| --- | --- |
| 多项式 | `0x1021` |
| 初始值 | `0xFFFF` |
| 输入反射 | 否 |
| 输出反射 | 否 |
| 异或输出 | `0x0000` |

## 接收状态机

接收端按以下顺序解析：

1. 搜索 `AA 55`。
2. 读取固定头部。
3. 检查 `version` 和 `length`。
4. 收齐载荷和 CRC。
5. 校验 CRC。
6. 把完整帧投递到消息处理器。

出现以下情况时丢弃当前帧并重新搜索 `SOF`：

- 版本不支持。
- 长度超过 256。
- CRC 不匹配。
- 接收超时。

接收端记录 `crc_error_count`、`length_error_count`、`unknown_type_count` 和 `rx_overflow_count`。

## 消息类型

| 类型 | 名称 | 方向 | 用途 |
| ---: | --- | --- | --- |
| `0x01` | `HELLO` | 双向 | 建立链路并交换能力 |
| `0x02` | `HEARTBEAT` | 双向 | 周期上报健康状态 |
| `0x10` | `TELEMETRY` | STM32 到 ESP32-S3 | 上报采集和输出快照 |
| `0x11` | `EVENT` | STM32 到 ESP32-S3 | 上报输入变化和继电器变化 |
| `0x12` | `DIAGNOSTICS` | STM32 到 ESP32-S3 | 上报任务活性、栈余量和错误计数 |
| `0x20` | `COMMAND` | ESP32-S3 到 STM32 | 请求控制和配置操作 |
| `0x21` | `COMMAND_ACK` | STM32 到 ESP32-S3 | 返回命令结果 |

## HELLO

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `role` | `uint8` | `1` 表示 STM32，`2` 表示 ESP32-S3 |
| `firmware_version` | `uint16` | 高 8 位主版本，低 8 位次版本 |
| `capabilities` | `uint32` | 能力位图 |

能力位：

| 位 | 能力 |
| ---: | --- |
| 0 | 模拟采集 |
| 1 | 温度采集 |
| 2 | 数字输入 |
| 3 | 继电器输出 |
| 4 | 模拟输出 |
| 5 | RS485 |
| 6 | RS232 |
| 7 | CAN |
| 8 | EEPROM |
| 9 | LCD |
| 10 | 音频 |

## HEARTBEAT

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `uptime_ms` | `uint32` | 启动后的毫秒数 |
| `health_flags` | `uint16` | 健康状态位 |
| `active_faults` | `uint16` | 当前故障位 |

健康位：

| 位 | 含义 |
| ---: | --- |
| 0 | 初始化完成 |
| 1 | 配置有效 |
| 2 | 现场总线正常 |
| 3 | 网络正常 |
| 4 | 存储正常 |

默认每秒发送一次心跳。连续 3 秒没有收到有效状态或心跳时，链路标记为离线。

## TELEMETRY

| 字段 | 类型 | 数量 | 说明 |
| --- | --- | ---: | --- |
| `timestamp_ms` | `uint32` | 1 | STM32 运行时间 |
| `ai_raw` | `int32` | 8 | ADS1256 原始值 |
| `rtd_millicelsius` | `int32` | 1 | 温度，单位 0.001 摄氏度 |
| `di_bits` | `uint8` | 1 | 数字输入位图 |
| `relay_bits` | `uint8` | 1 | 继电器状态位图 |
| `supply_mv` | `uint16` | 2 | 24V 和 5V 监测值 |
| `fault_bits` | `uint16` | 1 | 当前故障位图 |

当前固件的默认周期为 100 ms。协议允许在链路拥塞时跳过遥测帧，事件和 ACK 使用独立路径。

## EVENT

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `timestamp_ms` | `uint32` | 事件时间 |
| `event_code` | `uint16` | 事件类型 |
| `argument0` | `uint32` | 事件参数 0 |
| `argument1` | `uint32` | 事件参数 1 |

事件类型：

| 编码 | 名称 | 参数 |
| ---: | --- | --- |
| `0x0001` | 数字输入变化 | `argument0` 为输入位图，`argument1` 为变化位 |
| `0x0002` | 继电器状态变化 | `argument0` 为输出位图 |
| `0x0003` | 模拟量报警 | `argument0` 为通道，`argument1` 为阈值 |
| `0x0004` | 温度报警 | `argument0` 为报警类型 |
| `0x0005` | 电源异常 | `argument0` 为电源通道 |
| `0x0006` | 通信故障 | `argument0` 为接口编号 |
| `0x0007` | 配置恢复默认值 | 扩展编码 |

当前固件实际发送 `0x0001` 和 `0x0002`，其余编码作为扩展接口保留。

## DIAGNOSTICS

| 字段 | 类型 | 数量 | 说明 |
| --- | --- | ---: | --- |
| `timestamp_ms` | `uint32` | 1 | STM32 运行时间 |
| `task_alive_bits` | `uint32` | 1 | 6 个任务的存活位图 |
| `uart_rx_dropped` | `uint32` | 1 | UART 接收队列丢包计数 |
| `event_queue_dropped` | `uint32` | 1 | 事件队列丢包计数 |
| `watchdog_refresh_count` | `uint32` | 1 | IWDG 刷新次数 |
| `task_stack_free` | `uint16` | 6 | 各任务最小剩余栈空间 |

任务位和栈空间顺序固定为：

```text
bit 0 / index 0: controlTask
bit 1 / index 1: acqTask
bit 2 / index 2: monitorTask
bit 3 / index 3: rtdTask
bit 4 / index 4: bridgeTask
bit 5 / index 5: modbusTask
```

当前固件每 5 秒发送一次诊断帧。ESP32-S3 收到后更新状态缓存，并在控制台执行 `status` 时显示。

## COMMAND

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `request_id` | `uint16` | 请求编号 |
| `command_id` | `uint16` | 命令编号 |
| `argument0` | `uint32` | 参数 0 |
| `argument1` | `uint32` | 参数 1 |
| `argument2` | `uint32` | 参数 2 |

命令编号：

| 编码 | 名称 | 参数 |
| ---: | --- | --- |
| `0x0001` | 设置继电器掩码 | `argument0` 为目标位图，`argument1` 为有效位掩码 |
| `0x0002` | 脉冲继电器 | `argument0` 为通道，`argument1` 为毫秒数 |
| `0x0003` | 设置模拟输出 | `argument0` 为通道，`argument1` 为输出值 |
| `0x0004` | 清除锁存故障 | `argument0` 为故障位掩码 |
| `0x0100` | 开始模拟量校准 | 扩展编码 |
| `0x0101` | 开始温度校准 | 扩展编码 |
| `0x0200` | 保存配置 | 参数保留 |
| `0x0201` | 恢复默认配置 | `argument0` 为确认码 |

## COMMAND_ACK

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `request_id` | `uint16` | 与请求相同 |
| `command_id` | `uint16` | 与请求相同 |
| `result` | `int16` | 结果编码 |
| `detail` | `uint16` | 补充信息 |

结果编码：

| 编码 | 名称 | 说明 |
| ---: | --- | --- |
| `0` | `OK` | 命令完成 |
| `1` | `UNSUPPORTED` | 命令不支持 |
| `2` | `INVALID_ARGUMENT` | 参数无效 |
| `3` | `BUSY` | 资源忙，可稍后重试 |
| `4` | `NOT_READY` | 模块未初始化 |
| `5` | `OUT_OF_RANGE` | 数值超出范围 |
| `6` | `STORAGE_ERROR` | 存储读写失败 |
| `7` | `SAFETY_LOCK` | 安全联锁阻止操作 |

## 命令时序

ESP32-S3 发出命令后等待 500 ms。超时后复用原 `request_id` 重试，最多重试两次。STM32 保存最近 16 个请求结果，收到重复请求时直接返回原结果，不再次执行输出。

ESP32-S3 使用 `request_id` 和 `command_id` 匹配 `COMMAND_ACK`。收到 `OK` 后更新命令状态，收到错误后显示错误原因，不自行假定命令成功。

## 链路恢复

链路启动后，双方发送 `HELLO`。收到对方的 `HELLO` 后开始发送心跳和遥测。

UART 重新同步时，接收端从输入流中搜索最新 `AA 55`。未完成载荷和计数统计保留在诊断信息中。

## 版本兼容

`version` 为 `0x01`。接收端拒绝未知主版本。新增消息类型和命令编号时保持旧消息布局不变。
