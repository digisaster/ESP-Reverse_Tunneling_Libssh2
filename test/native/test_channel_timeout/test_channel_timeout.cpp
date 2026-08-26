#include "../../../src/channel_timeout.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_30_minute_timeout_does_not_expire_after_30_seconds(void) {
  TEST_ASSERT_FALSE(channel_timeout::expired(31001UL, 1000UL, 1800000UL));
}

void test_configured_timeout_expires_after_threshold(void) {
  TEST_ASSERT_FALSE(channel_timeout::expired(31000UL, 1000UL, 30000UL));
  TEST_ASSERT_TRUE(channel_timeout::expired(31001UL, 1000UL, 30000UL));
}

void test_zero_timeout_disables_idle_close(void) {
  TEST_ASSERT_FALSE(channel_timeout::expired(4000000000UL, 1UL, 0UL));
}

void test_elapsed_time_handles_millis_wraparound(void) {
  const unsigned long lastActivity = 0xFFFFFFF0UL;
  const unsigned long now = 0x00000020UL;
  TEST_ASSERT_FALSE(channel_timeout::expired(now, lastActivity, 48UL));
  TEST_ASSERT_TRUE(channel_timeout::expired(now, lastActivity, 47UL));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_30_minute_timeout_does_not_expire_after_30_seconds);
  RUN_TEST(test_configured_timeout_expires_after_threshold);
  RUN_TEST(test_zero_timeout_disables_idle_close);
  RUN_TEST(test_elapsed_time_handles_millis_wraparound);
  return UNITY_END();
}
