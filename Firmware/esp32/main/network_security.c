#include "network_security.h"

#include "bridge_protocol.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define NETWORK_SECURITY_RATE_LIMIT_MS 100U

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static NetworkSecurityAuditEntry s_audit[NETWORK_SECURITY_AUDIT_SIZE];
static uint8_t s_audit_head;
static uint8_t s_audit_count;
static uint32_t s_last_output_ms;
static bool s_has_last_output;

static bool NetworkSecurity_IsOutputCommand(uint16_t command_id)
{
  return (command_id == BRIDGE_CMD_SET_RELAY_MASK) ||
         (command_id == BRIDGE_CMD_PULSE_RELAY) ||
         (command_id == BRIDGE_CMD_SET_ANALOG_OUTPUT);
}

void NetworkSecurity_Init(void)
{
  portENTER_CRITICAL(&s_lock);
  s_audit_head = 0U;
  s_audit_count = 0U;
  s_last_output_ms = 0U;
  s_has_last_output = false;
  portEXIT_CRITICAL(&s_lock);
}

bool NetworkSecurity_AllowOutputCommand(uint16_t command_id)
{
  uint32_t now_ms;
  bool allowed = true;

  if (!NetworkSecurity_IsOutputCommand(command_id))
  {
    return true;
  }

  now_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
  portENTER_CRITICAL(&s_lock);
  if (s_has_last_output &&
      ((uint32_t)(now_ms - s_last_output_ms) < NETWORK_SECURITY_RATE_LIMIT_MS))
  {
    allowed = false;
  }
  else
  {
    s_has_last_output = true;
    s_last_output_ms = now_ms;
  }
  portEXIT_CRITICAL(&s_lock);

  return allowed;
}

void NetworkSecurity_Record(uint16_t request_id,
                            uint16_t command_id,
                            int result,
                            uint16_t detail)
{
  NetworkSecurityAuditEntry *entry;

  portENTER_CRITICAL(&s_lock);
  entry = &s_audit[s_audit_head];
  entry->request_id = request_id;
  entry->command_id = command_id;
  entry->result = (int16_t)result;
  entry->detail = detail;
  entry->uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
  s_audit_head = (uint8_t)((s_audit_head + 1U) % NETWORK_SECURITY_AUDIT_SIZE);
  if (s_audit_count < NETWORK_SECURITY_AUDIT_SIZE)
  {
    s_audit_count++;
  }
  portEXIT_CRITICAL(&s_lock);
}

uint8_t NetworkSecurity_GetAudit(NetworkSecurityAuditEntry *entries,
                                 uint8_t max_entries)
{
  uint8_t count;
  uint8_t start;

  if ((entries == NULL) || (max_entries == 0U))
  {
    return 0U;
  }

  portENTER_CRITICAL(&s_lock);
  count = (s_audit_count < max_entries) ? s_audit_count : max_entries;
  start = (uint8_t)((s_audit_head + NETWORK_SECURITY_AUDIT_SIZE - count) %
                    NETWORK_SECURITY_AUDIT_SIZE);
  for (uint8_t index = 0U; index < count; index++)
  {
    entries[index] = s_audit[(start + index) % NETWORK_SECURITY_AUDIT_SIZE];
  }
  portEXIT_CRITICAL(&s_lock);

  return count;
}
