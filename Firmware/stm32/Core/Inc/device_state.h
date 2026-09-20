#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define DEVICE_FAULT_SUPPLY_24V (1U << 0)
#define DEVICE_FAULT_SUPPLY_5V  (1U << 1)
#define DEVICE_FAULT_RTD        (1U << 2)
#define DEVICE_FAULT_ADS1256    (1U << 3)
#define DEVICE_FAULT_CONFIG     (1U << 4)
#define DEVICE_FAULT_COMM       (1U << 5)
#define DEVICE_FAULT_RUNTIME    (1U << 6)

#define DEVICE_TASK_COUNT       5U

typedef struct
{
  uint32_t uptime_ms;
  uint32_t uart_rx_event_count;
  uint16_t supply_24v_raw;
  uint16_t supply_5v_raw;
  uint16_t supply_24v_mv;
  uint16_t supply_5v_mv;
  int32_t ai_raw[8];
  int32_t rtd_millicelsius;
  uint8_t di_bits;
  uint8_t relay_bits;
  uint16_t analog_output_raw[2];
  uint16_t fault_bits;
  uint32_t task_alive_bits;
  uint16_t task_stack_free[DEVICE_TASK_COUNT];
  uint32_t uart_rx_dropped;
  uint32_t event_queue_dropped;
  uint32_t watchdog_refresh_count;
} DeviceStateSnapshot;

void DeviceState_Init(void);
void DeviceState_UpdateSupplies(uint16_t raw_24v, uint16_t raw_5v,
                                uint16_t mv_24v, uint16_t mv_5v);
void DeviceState_UpdateUart(uint32_t rx_event_count);
void DeviceState_UpdateAnalogInputs(const int32_t ai_raw[8]);
void DeviceState_UpdateTemperature(int32_t rtd_millicelsius, uint8_t fault);
void DeviceState_UpdateDigital(uint8_t di_bits, uint8_t relay_bits);
void DeviceState_UpdateAnalogOutput(uint8_t channel, uint16_t value);
void DeviceState_UpdateRuntimeDiagnostics(uint32_t task_alive_bits,
                                          const uint16_t task_stack_free[DEVICE_TASK_COUNT],
                                          uint32_t uart_rx_dropped,
                                          uint32_t event_queue_dropped,
                                          uint32_t watchdog_refresh_count);
void DeviceState_ClearFaults(uint16_t fault_mask);
void DeviceState_SetFault(uint16_t fault_mask, uint8_t active);
DeviceStateSnapshot DeviceState_Get(void);

#ifdef __cplusplus
}
#endif

#endif
