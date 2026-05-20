#ifndef FORWARD_ACCEPT_ERROR_H
#define FORWARD_ACCEPT_ERROR_H

namespace forward_accept_error {

inline bool isBenignNoChannel(int err, int eagainCode) {
  return err == 0 || err == eagainCode;
}

// CHANNEL_UNKNOWN is intentionally NOT treated as fatal: libssh2 returns it
// from libssh2_channel_forward_accept whenever the transport processed
// non-EAGAIN traffic (keepalive reply, window adjust, channel data on another
// channel) with an empty listener queue. The session is healthy.
inline bool isFatal(int err, int eagainCode, int channelClosedCode,
                    int socketSendCode, int socketDisconnectCode) {
  if (isBenignNoChannel(err, eagainCode)) {
    return false;
  }
  return err == channelClosedCode || err == socketSendCode ||
         err == socketDisconnectCode;
}

inline bool shouldReconnectAfterConsecutiveErrors(int consecutiveFatalErrors,
                                                  int err, int eagainCode,
                                                  int channelClosedCode,
                                                  int socketSendCode,
                                                  int socketDisconnectCode) {
  return consecutiveFatalErrors >= 3 &&
         isFatal(err, eagainCode, channelClosedCode, socketSendCode,
                 socketDisconnectCode);
}

} // namespace forward_accept_error

#endif // FORWARD_ACCEPT_ERROR_H
