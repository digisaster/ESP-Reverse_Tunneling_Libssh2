#ifndef MEMORY_FIXES_H
#define MEMORY_FIXES_H

#include "logger.h"
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Feature levels (configurable via -DMEMFIX_LEVEL=<0|1|2>)
// 0 = OFF: no heap check, no memset, minimal overhead
// 1 = LIGHT: error logs if malloc fails, no heap check or memset
// 2 = FULL (default): periodic heap check and memset of allocated buffers
#ifndef MEMFIX_LEVEL
#define MEMFIX_LEVEL 2
#endif

// Macros to improve memory management
#define SAFE_FREE(ptr)                                                         \
  do {                                                                         \
    if ((ptr) != nullptr) {                                                    \
      free(ptr);                                                               \
      (ptr) = nullptr;                                                         \
    }                                                                          \
  } while (0)

#define SAFE_DELETE_SEMAPHORE(sem)                                             \
  do {                                                                         \
    if ((sem) != NULL) {                                                       \
      vSemaphoreDelete(sem);                                                   \
      (sem) = NULL;                                                            \
    }                                                                          \
  } while (0)

// Check whether the requested allocation still leaves a small operating
// reserve. Unlike the old fixed 50KB warning, this remains meaningful on
// low-memory ESP32 variants that are healthy below that value.
inline void checkHeapHealth(size_t requestedBytes = 0) {
#if MEMFIX_LEVEL >= 2
  size_t freeHeap = ESP.getFreeHeap();
  size_t minFreeHeap = ESP.getMinFreeHeap();
  static constexpr size_t HEAP_RESERVE_BYTES = 8 * 1024;
  if (freeHeap < requestedBytes + HEAP_RESERVE_BYTES) {
    LOGF_W("MEM", "Low heap for %u-byte allocation: free %u (min: %u)",
           (unsigned)requestedBytes, (unsigned)freeHeap, (unsigned)minFreeHeap);
  }
  size_t largestFreeBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (requestedBytes > 0 && largestFreeBlock < requestedBytes) {
    LOGF_W("MEM", "Allocation at risk: need %u, largest block %u",
           (unsigned)requestedBytes, (unsigned)largestFreeBlock);
  }
#else
  // No-op in OFF/LIGHT modes
#endif
}

// Safe allocation with verification
inline void *safeMalloc(size_t size, const char *tag = "UNKNOWN") {
#if MEMFIX_LEVEL >= 2
  checkHeapHealth(size);
#endif
  void *ptr = malloc(size);
  if (ptr == nullptr) {
#if MEMFIX_LEVEL >= 1
    LOGF_E("MEM", "malloc failed (%u bytes) tag=%s", (unsigned)size, tag);
#endif
    return nullptr;
  }
#if MEMFIX_LEVEL >= 2
  memset(ptr, 0, size);
#endif
#if MEMFIX_LEVEL >= 2
  LOGF_D("MEM", "Allocated %u bytes for %s at %p", (unsigned)size, tag, ptr);
#endif
  return ptr;
}
// Safe reallocation with verification
inline void *safeRealloc(void *ptr, size_t size, const char *tag = "UNKNOWN") {
#if MEMFIX_LEVEL >= 2
  checkHeapHealth(size);
#endif
  void *newPtr = realloc(ptr, size);
  if (newPtr == nullptr && size > 0) {
#if MEMFIX_LEVEL >= 1
    LOGF_E("MEM", "realloc failed (%u bytes) tag=%s", (unsigned)size, tag);
#endif
    return nullptr;
  }
#if MEMFIX_LEVEL >= 2
  LOGF_D("MEM", "Reallocated %u bytes for %s at %p (was %p)", (unsigned)size,
         tag, newPtr, ptr);
#endif
  return newPtr;
}
// Periodic heap maintenance
inline void performHeapMaintenance() {
#if MEMFIX_LEVEL >= 2
  heap_caps_check_integrity_all(true);
  size_t total_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t total_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  LOGF_D("MEM", "Free: %u internal, %u total", (unsigned)total_internal,
         (unsigned)total_free);
#else
  // No-op in OFF/LIGHT modes
#endif
}

#endif // MEMORY_FIXES_H
