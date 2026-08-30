#include "../../../src/channel_memory_guard.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_c3_observed_heap_is_accepted(void) {
  const auto required = channel_memory_guard::requirements(8192, 4096);

  TEST_ASSERT_EQUAL_size_t(32768, required.totalBytes);
  TEST_ASSERT_EQUAL_size_t(8192, required.largestBlockBytes);
  TEST_ASSERT_TRUE(
      channel_memory_guard::hasCapacity(77764, 32756, required));
}

void test_rejects_when_total_memory_is_insufficient(void) {
  const auto required = channel_memory_guard::requirements(8192, 4096);

  TEST_ASSERT_FALSE(
      channel_memory_guard::hasCapacity(32767, 32756, required));
}

void test_rejects_when_largest_block_is_too_small(void) {
  const auto required = channel_memory_guard::requirements(8192, 4096);

  TEST_ASSERT_FALSE(
      channel_memory_guard::hasCapacity(77764, 8191, required));
}

void test_accepts_exact_boundaries(void) {
  const auto required = channel_memory_guard::requirements(8192, 4096);

  TEST_ASSERT_TRUE(channel_memory_guard::hasCapacity(
      required.totalBytes, required.largestBlockBytes, required));
}

void test_psram_rejects_only_one_ring_sized_block(void) {
  const auto required = channel_memory_guard::requirements(64 * 1024, 8192);

  TEST_ASSERT_TRUE(
      channel_memory_guard::hasCapacity(256 * 1024, 64 * 1024, required));
  TEST_ASSERT_FALSE(
      channel_memory_guard::hasContiguousCapacity(64 * 1024, required));
}

void test_psram_accepts_complete_contiguous_budget(void) {
  const auto required = channel_memory_guard::requirements(64 * 1024, 8192);

  TEST_ASSERT_TRUE(channel_memory_guard::hasContiguousCapacity(
      required.totalBytes, required));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_c3_observed_heap_is_accepted);
  RUN_TEST(test_rejects_when_total_memory_is_insufficient);
  RUN_TEST(test_rejects_when_largest_block_is_too_small);
  RUN_TEST(test_accepts_exact_boundaries);
  RUN_TEST(test_psram_rejects_only_one_ring_sized_block);
  RUN_TEST(test_psram_accepts_complete_contiguous_budget);
  return UNITY_END();
}
