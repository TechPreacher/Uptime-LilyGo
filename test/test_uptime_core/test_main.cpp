#include <UptimeCore.h>
#include <unity.h>

#include <cstdint>

using uptime_core::SiteState;

namespace {

uptime_core::StateCounts countStates(const SiteState *states, size_t count) {
  uptime_core::StateCounts counts;
  for (size_t index = 0; index < count; ++index) {
    uptime_core::addState(counts, states[index]);
  }
  return counts;
}

void test_http_status_boundaries() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SiteState::Down),
                        static_cast<int>(uptime_core::classifyHttpStatus(-1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SiteState::Down),
                        static_cast<int>(uptime_core::classifyHttpStatus(199)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SiteState::Up),
                        static_cast<int>(uptime_core::classifyHttpStatus(200)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SiteState::Up),
                        static_cast<int>(uptime_core::classifyHttpStatus(399)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SiteState::Down),
                        static_cast<int>(uptime_core::classifyHttpStatus(400)));
}

void test_orientation_mapping() {
  using uptime_core::Orientation;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Orientation::Right),
                        static_cast<int>(uptime_core::parseOrientation("right")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Orientation::Down),
                        static_cast<int>(uptime_core::parseOrientation("down")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Orientation::Left),
                        static_cast<int>(uptime_core::parseOrientation("left")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Orientation::Up),
                        static_cast<int>(uptime_core::parseOrientation("up")));
  TEST_ASSERT_EQUAL_UINT8(0, uptime_core::displayRotation(Orientation::Right));
  TEST_ASSERT_EQUAL_UINT8(1, uptime_core::displayRotation(Orientation::Down));
  TEST_ASSERT_EQUAL_UINT8(2, uptime_core::displayRotation(Orientation::Left));
  TEST_ASSERT_EQUAL_UINT8(3, uptime_core::displayRotation(Orientation::Up));
}

void test_invalid_orientation_defaults_to_right() {
  using uptime_core::Orientation;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Orientation::Right),
                        static_cast<int>(uptime_core::parseOrientation("diagonal")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Orientation::Right),
                        static_cast<int>(uptime_core::parseOrientation(nullptr)));
}

void test_unknown_states_are_not_counted() {
  const SiteState states[] = {SiteState::Unknown, SiteState::Up, SiteState::Down,
                              SiteState::Unknown};
  const uptime_core::StateCounts counts = countStates(states, 4);

  TEST_ASSERT_EQUAL_UINT32(1, counts.up);
  TEST_ASSERT_EQUAL_UINT32(1, counts.down);
}

void test_up_to_down_transition_updates_counts() {
  SiteState states[] = {SiteState::Up, SiteState::Up, SiteState::Down};
  uptime_core::StateCounts counts = countStates(states, 3);
  TEST_ASSERT_EQUAL_UINT32(2, counts.up);
  TEST_ASSERT_EQUAL_UINT32(1, counts.down);

  states[0] = SiteState::Down;
  counts = countStates(states, 3);
  TEST_ASSERT_EQUAL_UINT32(1, counts.up);
  TEST_ASSERT_EQUAL_UINT32(2, counts.down);
}

void test_brightness_conversion() {
  TEST_ASSERT_EQUAL_UINT8(0, uptime_core::brightnessToByte(0));
  TEST_ASSERT_EQUAL_UINT8(128, uptime_core::brightnessToByte(50));
  TEST_ASSERT_EQUAL_UINT8(191, uptime_core::brightnessToByte(75));
  TEST_ASSERT_EQUAL_UINT8(255, uptime_core::brightnessToByte(100));
  TEST_ASSERT_EQUAL_UINT8(255, uptime_core::brightnessToByte(255));
}

void test_interval_boundary() {
  TEST_ASSERT_FALSE(uptime_core::intervalElapsed(1099, 100, 1000));
  TEST_ASSERT_TRUE(uptime_core::intervalElapsed(1100, 100, 1000));
}

void test_interval_handles_millis_rollover() {
  const uint32_t previous = UINT32_MAX - 24U;
  TEST_ASSERT_FALSE(uptime_core::intervalElapsed(24, previous, 50));
  TEST_ASSERT_TRUE(uptime_core::intervalElapsed(25, previous, 50));
}

}  // namespace

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_http_status_boundaries);
  RUN_TEST(test_orientation_mapping);
  RUN_TEST(test_invalid_orientation_defaults_to_right);
  RUN_TEST(test_unknown_states_are_not_counted);
  RUN_TEST(test_up_to_down_transition_updates_counts);
  RUN_TEST(test_brightness_conversion);
  RUN_TEST(test_interval_boundary);
  RUN_TEST(test_interval_handles_millis_rollover);
  return UNITY_END();
}