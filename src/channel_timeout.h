#ifndef CHANNEL_TIMEOUT_H
#define CHANNEL_TIMEOUT_H

namespace channel_timeout {

inline bool expired(unsigned long now, unsigned long lastActivity,
                    unsigned long timeoutMs) {
  return timeoutMs > 0 && lastActivity > 0 &&
         static_cast<unsigned long>(now - lastActivity) > timeoutMs;
}

} // namespace channel_timeout

#endif // CHANNEL_TIMEOUT_H
