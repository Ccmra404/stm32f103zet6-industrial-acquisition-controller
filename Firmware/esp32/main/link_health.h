#ifndef LINK_HEALTH_H
#define LINK_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#define LINK_HEALTH_EVENT_HISTORY_SIZE 8U

typedef enum
{
  LINK_HEALTH_BOOT = 0,
  LINK_HEALTH_ONLINE,
  LINK_HEALTH_DEGRADED,
  LINK_HEALTH_OFFLINE,
} LinkHealthState;

typedef struct
{
  LinkHealthState state;
  uint32_t last_rx_age_ms;
  uint32_t rx_frames;
  uint32_t timeout_count;
  uint32_t reconnect_count;
  bool command_allowed;
} LinkHealthSnapshot;

typedef struct
{
  LinkHealthState state;
  uint32_t uptime_ms;
  uint32_t reconnect_count;
  uint32_t timeout_count;
} LinkHealthEvent;

void LinkHealth_Init(void);
void LinkHealth_OnValidFrame(void);
void LinkHealth_Poll(void);
LinkHealthSnapshot LinkHealth_GetSnapshot(void);
uint8_t LinkHealth_GetEvents(LinkHealthEvent *events, uint8_t max_events);
const char *LinkHealth_StateName(LinkHealthState state);

#endif
