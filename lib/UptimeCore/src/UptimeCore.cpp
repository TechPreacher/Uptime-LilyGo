#include "UptimeCore.h"

namespace uptime_core {

SiteState classifyHttpStatus(int statusCode) {
  return statusCode >= 200 && statusCode < 400 ? SiteState::Up : SiteState::Down;
}

void addState(StateCounts &counts, SiteState state) {
  if (state == SiteState::Up) {
    ++counts.up;
  } else if (state == SiteState::Down) {
    ++counts.down;
  }
}

uint8_t brightnessToByte(uint8_t percent) {
  const uint8_t boundedPercent = percent > 100 ? 100 : percent;
  return static_cast<uint8_t>(
      (static_cast<uint16_t>(boundedPercent) * 255U + 50U) / 100U);
}

bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval) {
  return now - previous >= interval;
}

}  // namespace uptime_core