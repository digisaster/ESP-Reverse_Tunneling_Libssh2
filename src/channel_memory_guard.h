#ifndef CHANNEL_MEMORY_GUARD_H
#define CHANNEL_MEMORY_GUARD_H

#include <cstddef>

namespace channel_memory_guard {

struct Requirements {
  size_t totalBytes;
  size_t largestBlockBytes;
};

inline Requirements requirements(size_t ringBufferBytes,
                                 size_t prependBufferBytes,
                                 size_t reserveBytes = 8192) {
  Requirements result;
  result.totalBytes =
      ringBufferBytes * 2 + prependBufferBytes * 2 + reserveBytes;
  result.largestBlockBytes = ringBufferBytes > prependBufferBytes
                                 ? ringBufferBytes
                                 : prependBufferBytes;
  return result;
}

inline bool hasCapacity(size_t totalFreeBytes, size_t largestFreeBlockBytes,
                        const Requirements &required) {
  return totalFreeBytes >= required.totalBytes &&
         largestFreeBlockBytes >= required.largestBlockBytes;
}

// Conservative PSRAM policy: require one contiguous region for the complete
// storage budget. This guarantees that both ring-sized allocations can remain
// in PSRAM instead of silently falling back to internal heap when PSRAM is
// fragmented.
inline bool hasContiguousCapacity(size_t largestFreeBlockBytes,
                                  const Requirements &required) {
  return largestFreeBlockBytes >= required.totalBytes;
}

} // namespace channel_memory_guard

#endif // CHANNEL_MEMORY_GUARD_H
