#pragma once

#include <cstddef>
#include <cstdint>

namespace uptime_core {

enum class SiteState : uint8_t { Unknown, Up, Down };

struct StateCounts {
  size_t up = 0;
  size_t down = 0;
};

SiteState classifyHttpStatus(int statusCode);
void addState(StateCounts &counts, SiteState state);
uint8_t brightnessToByte(uint8_t percent);
bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval);

}  // namespace uptime_core