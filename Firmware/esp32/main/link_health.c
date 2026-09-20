#include "link_health.h"

#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define LINK_HEALTH_DEGRADED_AFTER_US 1500000LL
#define LINK_HEALTH_OFFLINE_AFTER_US 3000000LL

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static LinkHealthState s_state;
static int64_t s_last_rx_us;
static uint32_t s_rx_frames;
static uint32_t s_timeout_count;
static uint32_t s_reconnect_count;
static LinkHealthEvent s_events[LINK_HEALTH_EVENT_HISTORY_SIZE];
static uint8_t s_event_head;
static uint8_t s_event_count;

static void LinkHealth_RecordEventLocked(LinkHealthState state)
{
  LinkHealthEvent *event = &s_events[s_event_head];

  event->state = state;
  event->uptime_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
  event->reconnect_count = s_reconnect_count;
  event->timeout_count = s_timeout_count;

  s_event_head = (uint8_t)((s_event_head + 1U) % LINK_HEALTH_EVENT_HISTORY_SIZE);
  if (s_event_count < LINK_HEALTH_EVENT_HISTORY_SIZE)
  {
    s_event_count++;
  }
}

static uint32_t LinkHealth_AgeMs(int64_t now_us)
{
  if (s_last_rx_us <= 0)
  {
    return 0U;
  }

  if (now_us <= s_last_rx_us)
  {
    return 0U;
  }

  return (uint32_t)((now_us - s_last_rx_us) / 1000LL);
}

void LinkHealth_Init(void)
{
  portENTER_CRITICAL(&s_lock);
  s_state = LINK_HEALTH_BOOT;
  s_last_rx_us = 0;
  s_rx_frames = 0U;
  s_timeout_count = 0U;
  s_reconnect_count = 0U;
  memset(s_events, 0, sizeof(s_events));
  s_event_head = 0U;
  s_event_count = 0U;
  portEXIT_CRITICAL(&s_lock);
}

void LinkHealth_OnValidFrame(void)
{
  int64_t now_us = esp_timer_get_time();

  portENTER_CRITICAL(&s_lock);
  if (s_state == LINK_HEALTH_OFFLINE)
  {
    s_reconnect_count++;
  }
  s_last_rx_us = now_us;
  s_rx_frames++;
  if (s_state != LINK_HEALTH_ONLINE)
  {
    s_state = LINK_HEALTH_ONLINE;
    LinkHealth_RecordEventLocked(LINK_HEALTH_ONLINE);
  }
  portEXIT_CRITICAL(&s_lock);
}

void LinkHealth_Poll(void)
{
  int64_t now_us = esp_timer_get_time();

  portENTER_CRITICAL(&s_lock);
  if (s_last_rx_us <= 0)
  {
    s_state = LINK_HEALTH_BOOT;
  }
  else if ((now_us - s_last_rx_us) > LINK_HEALTH_OFFLINE_AFTER_US)
  {
    if (s_state != LINK_HEALTH_OFFLINE)
    {
      s_state = LINK_HEALTH_OFFLINE;
      s_timeout_count++;
      LinkHealth_RecordEventLocked(LINK_HEALTH_OFFLINE);
    }
  }
  else if ((now_us - s_last_rx_us) > LINK_HEALTH_DEGRADED_AFTER_US)
  {
    if (s_state != LINK_HEALTH_OFFLINE)
    {
      if (s_state != LINK_HEALTH_DEGRADED)
      {
        s_state = LINK_HEALTH_DEGRADED;
        LinkHealth_RecordEventLocked(LINK_HEALTH_DEGRADED);
      }
    }
  }
  else
  {
    if (s_state != LINK_HEALTH_ONLINE)
    {
      s_state = LINK_HEALTH_ONLINE;
      LinkHealth_RecordEventLocked(LINK_HEALTH_ONLINE);
    }
  }
  portEXIT_CRITICAL(&s_lock);
}

LinkHealthSnapshot LinkHealth_GetSnapshot(void)
{
  int64_t now_us = esp_timer_get_time();
  LinkHealthSnapshot snapshot = {0};

  portENTER_CRITICAL(&s_lock);
  snapshot.state = s_state;
  snapshot.last_rx_age_ms = LinkHealth_AgeMs(now_us);
  snapshot.rx_frames = s_rx_frames;
  snapshot.timeout_count = s_timeout_count;
  snapshot.reconnect_count = s_reconnect_count;
  snapshot.command_allowed = (s_state == LINK_HEALTH_ONLINE);
  portEXIT_CRITICAL(&s_lock);

  return snapshot;
}

uint8_t LinkHealth_GetEvents(LinkHealthEvent *events, uint8_t max_events)
{
  uint8_t count;
  uint8_t start;

  if ((events == NULL) || (max_events == 0U))
  {
    return 0U;
  }

  portENTER_CRITICAL(&s_lock);
  count = (s_event_count < max_events) ? s_event_count : max_events;
  start = (uint8_t)((s_event_head + LINK_HEALTH_EVENT_HISTORY_SIZE - count) %
                    LINK_HEALTH_EVENT_HISTORY_SIZE);
  for (uint8_t index = 0U; index < count; index++)
  {
    events[index] = s_events[(start + index) % LINK_HEALTH_EVENT_HISTORY_SIZE];
  }
  portEXIT_CRITICAL(&s_lock);

  return count;
}

const char *LinkHealth_StateName(LinkHealthState state)
{
  switch (state)
  {
    case LINK_HEALTH_ONLINE:
      return "ONLINE";
    case LINK_HEALTH_DEGRADED:
      return "DEGRADED";
    case LINK_HEALTH_OFFLINE:
      return "OFFLINE";
    case LINK_HEALTH_BOOT:
    default:
      return "BOOT";
  }
}
