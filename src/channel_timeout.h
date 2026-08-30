#ifndef CHANNEL_TIMEOUT_H
#define CHANNEL_TIMEOUT_H

#include <cstdint>

namespace channel_timeout {

inline bool expired(uint32_t now, uint32_t lastActivity, uint32_t timeoutMs) {
  return timeoutMs > 0 && lastActivity > 0 &&
         static_cast<uint32_t>(now - lastActivity) > timeoutMs;
}

} // namespace channel_timeout

#endif // CHANNEL_TIMEOUT_H
