#pragma once

#include <Arduino.h>

namespace minis_public_key_upload {

// Uploads only the OpenSSH public key to the existing Minis uploot.php route.
// The private key never leaves the ESP32. The file is stored as
// /hb/<sid>/ui/ssh_public_key.txt on the Minis server.
bool upload(const String &publicKey);

} // namespace minis_public_key_upload
