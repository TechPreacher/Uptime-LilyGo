#pragma once

#include <cstddef>
#include <cstdint>

namespace uptime_core {

enum class SiteState : uint8_t { Unknown, Up, Down };
enum class Orientation : uint8_t { Right, Down, Left, Up };

struct StateCounts {
  size_t up = 0;
  size_t down = 0;
};

SiteState classifyHttpStatus(int statusCode);
Orientation parseOrientation(const char *value);
uint8_t displayRotation(Orientation orientation);
void addState(StateCounts &counts, SiteState state);
uint8_t brightnessToByte(uint8_t percent);
bool intervalElapsed(uint32_t now, uint32_t previous, uint32_t interval);

}  // namespace uptime_core