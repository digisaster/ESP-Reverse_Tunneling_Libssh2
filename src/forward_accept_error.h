#ifndef FORWARD_ACCEPT_ERROR_H
#define FORWARD_ACCEPT_ERROR_H

namespace forward_accept_error {

inline bool isBenignNoChannel(int err, int eagainCode) {
  return err == 0 || err == eagainCode;
}

// CHANNEL_UNKNOWN is treated as fatal: libssh2 returns it sporadically (once
// per keepalive reply etc.) when the listener queue is empty after a
// non-EAGAIN transport_read. Sporadic occurrences are harmless because any
// EAGAIN between them resets the consecutive counter. However, when sshd
// closes the connection due to `UnusedConnectionTimeout` (default-off but
// often enabled on hardened servers), tcpip-forward listeners are NOT
// counted as open channels (per sshd_config(5)), so the timeout fires and
// libssh2 starts seeing CHANNEL_UNKNOWN at ~100Hz on the closing channels.
// Treating it as fatal after 3 consecutive lets us reconnect cleanly
// instead of waiting for ClientAliveCountMax to kick in 30s later.
inline bool isFatal(int err, int eagainCode, int channelUnknownCode,
                    int channelClosedCode, int socketSendCode,
                    int socketDisconnectCode) {
  if (isBenignNoChannel(err, eagainCode)) {
    return false;
  }
  return err == channelUnknownCode || err == channelClosedCode ||
         err == socketSendCode || err == socketDisconnectCode;
}

inline bool shouldReconnectAfterConsecutiveErrors(int consecutiveFatalErrors,
                                                  int err, int eagainCode,
                                                  int channelUnknownCode,
                                                  int channelClosedCode,
                                                  int socketSendCode,
                                                  int socketDisconnectCode) {
  return consecutiveFatalErrors >= 3 &&
         isFatal(err, eagainCode, channelUnknownCode, channelClosedCode,
                 socketSendCode, socketDisconnectCode);
}

} // namespace forward_accept_error

#endif // FORWARD_ACCEPT_ERROR_H
