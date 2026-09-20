#ifndef NETWORK_SECURITY_H
#define NETWORK_SECURITY_H

#include <stdbool.h>
#include <stdint.h>

#define NETWORK_SECURITY_AUDIT_SIZE 16U

typedef struct
{
  uint16_t request_id;
  uint16_t command_id;
  int16_t result;
  uint16_t detail;
  uint32_t uptime_ms;
} NetworkSecurityAuditEntry;

void NetworkSecurity_Init(void);
bool NetworkSecurity_AllowOutputCommand(uint16_t command_id);
void NetworkSecurity_Record(uint16_t request_id,
                            uint16_t command_id,
                            int result,
                            uint16_t detail);
uint8_t NetworkSecurity_GetAudit(NetworkSecurityAuditEntry *entries,
                                 uint8_t max_entries);

#endif
