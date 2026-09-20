# 协议测试

`protocol_test.c` 直接编译 `Firmware/shared/protocol`，在主机上验证：

- CRC-16/CCITT-FALSE 标准向量。
- HEARTBEAT、TELEMETRY、DIAGNOSTICS、COMMAND 和 COMMAND_ACK 编解码。
- 小端序、序号、长度和字段值。
- 接收状态机在垃圾字节后重新同步。
- CRC 错误、版本错误和输出缓冲区过小时的拒绝路径。

在 Linux 或 GitHub Actions 中运行：

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -IFirmware/shared/protocol \
  Firmware/shared/protocol/bridge_protocol.c \
  Firmware/tests/protocol_test.c \
  -o protocol_test
./protocol_test
```

测试只依赖 C 标准库，不需要 STM32 HAL、FreeRTOS 或 ESP-IDF。

`modbus_test.c` 直接编译 `Firmware/stm32/Core/Src/modbus_rtu.c`，在主机上验证：

- Modbus RTU CRC-16 标准向量。
- 功能码 `0x03` 读保持寄存器。
- 功能码 `0x06` 写单寄存器。
- 非法功能码、非法地址和非法数量的异常响应。
- CRC 错误和从站地址不匹配时保持静默。

在 Linux 或 GitHub Actions 中运行：

```bash
gcc -std=c11 -Wall -Wextra -Werror \
  -IFirmware/stm32/Core/Inc \
  Firmware/stm32/Core/Src/modbus_rtu.c \
  Firmware/tests/modbus_test.c \
  -o modbus_test
./modbus_test
```
