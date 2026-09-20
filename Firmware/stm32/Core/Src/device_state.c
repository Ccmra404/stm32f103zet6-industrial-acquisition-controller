#include "device_state.h"

#include <string.h>

#include "cmsis_os.h"
#include "main.h"

static DeviceStateSnapshot s_state;
static osMutexId_t s_state_mutex;

static const osMutexAttr_t s_state_mutex_attributes = {
    .name = "deviceStateMutex",
};

static void StateLock(void)
{
  if (s_state_mutex != NULL)
  {
    (void)osMutexAcquire(s_state_mutex, osWaitForever);
  }
}

static void StateUnlock(void)
{
  if (s_state_mutex != NULL)
  {
    (void)osMutexRelease(s_state_mutex);
  }
}

void DeviceState_Init(void)
{
  s_state_mutex = osMutexNew(&s_state_mutex_attributes);
  if (s_state_mutex == NULL)
  {
    Error_Handler();
  }
}

void DeviceState_UpdateSupplies(uint16_t raw_24v, uint16_t raw_5v,
                                uint16_t mv_24v, uint16_t mv_5v)
{
  uint16_t faults;

  if ((mv_24v < 18000U) || (mv_24v > 30000U))
  {
    faults = DEVICE_FAULT_SUPPLY_24V;
  }
  else
  {
    faults = 0U;
  }

  if ((mv_5v < 4500U) || (mv_5v > 5500U))
  {
    faults |= DEVICE_FAULT_SUPPLY_5V;
  }

  StateLock();
  s_state.uptime_ms = HAL_GetTick();
  s_state.supply_24v_raw = raw_24v;
  s_state.supply_5v_raw = raw_5v;
  s_state.supply_24v_mv = mv_24v;
  s_state.supply_5v_mv = mv_5v;
  s_state.fault_bits = (uint16_t)((s_state.fault_bits &
                                   ~(DEVICE_FAULT_SUPPLY_24V | DEVICE_FAULT_SUPPLY_5V)) |
                                  faults);
  StateUnlock();
}

void DeviceState_UpdateUart(uint32_t rx_event_count)
{
  StateLock();
  s_state.uart_rx_event_count = rx_event_count;
  StateUnlock();
}

void DeviceState_UpdateAnalogInputs(const int32_t ai_raw[8])
{
  if (ai_raw == 0)
  {
    return;
  }

  StateLock();
  memcpy(s_state.ai_raw, ai_raw, sizeof(s_state.ai_raw));
  s_state.fault_bits &= (uint16_t)~DEVICE_FAULT_ADS1256;
  StateUnlock();
}

void DeviceState_UpdateTemperature(int32_t rtd_millicelsius, uint8_t fault)
{
  StateLock();
  s_state.rtd_millicelsius = rtd_millicelsius;
  if (fault != 0U)
  {
    s_state.fault_bits |= DEVICE_FAULT_RTD;
  }
  else
  {
    s_state.fault_bits &= (uint16_t)~DEVICE_FAULT_RTD;
  }
  StateUnlock();
}

void DeviceState_UpdateDigital(uint8_t di_bits, uint8_t relay_bits)
{
  StateLock();
  s_state.di_bits = di_bits;
  s_state.relay_bits = relay_bits;
  StateUnlock();
}

void DeviceState_UpdateAnalogOutput(uint8_t channel, uint16_t value)
{
  if (channel < 2U)
  {
    StateLock();
    s_state.analog_output_raw[channel] = value;
    StateUnlock();
  }
}

void DeviceState_UpdateRuntimeDiagnostics(uint32_t task_alive_bits,
                                          const uint16_t task_stack_free[DEVICE_TASK_COUNT],
                                          uint32_t uart_rx_dropped,
                                          uint32_t event_queue_dropped,
                                          uint32_t watchdog_refresh_count)
{
  if (task_stack_free == 0)
  {
    return;
  }

  StateLock();
  s_state.task_alive_bits = task_alive_bits;
  memcpy(s_state.task_stack_free, task_stack_free, sizeof(s_state.task_stack_free));
  s_state.uart_rx_dropped = uart_rx_dropped;
  s_state.event_queue_dropped = event_queue_dropped;
  s_state.watchdog_refresh_count = watchdog_refresh_count;
  StateUnlock();
}

void DeviceState_ClearFaults(uint16_t fault_mask)
{
  StateLock();
  s_state.fault_bits &= (uint16_t)~fault_mask;
  StateUnlock();
}

void DeviceState_SetFault(uint16_t fault_mask, uint8_t active)
{
  StateLock();
  if (active != 0U)
  {
    s_state.fault_bits |= fault_mask;
  }
  else
  {
    s_state.fault_bits &= (uint16_t)~fault_mask;
  }
  StateUnlock();
}

DeviceStateSnapshot DeviceState_Get(void)
{
  DeviceStateSnapshot snapshot;

  StateLock();
  snapshot = s_state;
  StateUnlock();

  return snapshot;
}
