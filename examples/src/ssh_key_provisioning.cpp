#include "ssh_key_provisioning.h"

#include "logger.h"
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>

namespace ssh_key_provisioning {
namespace {
constexpr char KEY_PATH[] = "/esp32tun_ssh_key";
constexpr char KEY_TEMP[] = "/esp32tun_ssh_key.tmp";
constexpr char PUBLIC_KEY_PATH[] = "/esp32tun_ssh_key.pub";
constexpr char PUBLIC_KEY_TEMP[] = "/esp32tun_ssh_key.pub.tmp";
constexpr size_t PRIVATE_PEM_BUFFER_SIZE = 1024;
constexpr size_t PUBLIC_DER_BUFFER_SIZE = 192;
constexpr size_t OPENSSH_BLOB_BUFFER_SIZE = 160;
constexpr size_t OPENSSH_BASE64_BUFFER_SIZE = 256;

void removeIfExists(const char *path) {
  if (LittleFS.exists(path))
    LittleFS.remove(path);
}

bool writeFile(const char *path, const String &value) {
  File file = LittleFS.open(path, "w");
  if (!file)
    return false;
  const size_t written = file.print(value);
  file.flush();
  file.close();
  return written == value.length();
}

bool loadExisting(String &privateKey, String &publicKey) {
  File privateFile = LittleFS.open(KEY_PATH, "r");
  File publicFile = LittleFS.open(PUBLIC_KEY_PATH, "r");
  if (!privateFile || !publicFile || privateFile.size() == 0 ||
      publicFile.size() == 0) {
    if (privateFile)
      privateFile.close();
    if (publicFile)
      publicFile.close();
    return false;
  }

  privateKey = privateFile.readString();
  publicKey = publicFile.readString();
  privateFile.close();
  publicFile.close();
  privateKey.trim();
  publicKey.trim();

  return privateKey.indexOf("-----BEGIN EC PRIVATE KEY-----") >= 0 &&
         privateKey.indexOf("-----END EC PRIVATE KEY-----") >= 0 &&
         publicKey.startsWith("ecdsa-sha2-nistp256 ");
}

bool appendUint32(unsigned char *buffer, size_t capacity, size_t &used,
                  uint32_t value) {
  if (used + 4 > capacity)
    return false;
  buffer[used++] = static_cast<unsigned char>((value >> 24) & 0xff);
  buffer[used++] = static_cast<unsigned char>((value >> 16) & 0xff);
  buffer[used++] = static_cast<unsigned char>((value >> 8) & 0xff);
  buffer[used++] = static_cast<unsigned char>(value & 0xff);
  return true;
}

bool appendBytes(unsigned char *buffer, size_t capacity, size_t &used,
                 const unsigned char *data, size_t length) {
  if (!appendUint32(buffer, capacity, used, static_cast<uint32_t>(length)) ||
      used + length > capacity)
    return false;
  memcpy(buffer + used, data, length);
  used += length;
  return true;
}

bool extractP256PointFromSpki(const unsigned char *der, size_t derLength,
                              unsigned char point[65]) {
  // mbedtls_pk_write_pubkey_der() emits SubjectPublicKeyInfo. For an
  // uncompressed P-256 key its BIT STRING ends in: 03 42 00 04 <64 bytes>.
  if (derLength < 69)
    return false;
  for (size_t i = 0; i + 69 <= derLength; ++i) {
    if (der[i] == 0x03 && der[i + 1] == 0x42 && der[i + 2] == 0x00 &&
        der[i + 3] == 0x04) {
      memcpy(point, der + i + 3, 65);
      return true;
    }
  }
  return false;
}

bool buildOpenSshPublicKey(mbedtls_pk_context &pk, String &publicKey) {
  unsigned char derBuffer[PUBLIC_DER_BUFFER_SIZE] = {0};
  const int derLength =
      mbedtls_pk_write_pubkey_der(&pk, derBuffer, sizeof(derBuffer));
  if (derLength <= 0 || static_cast<size_t>(derLength) > sizeof(derBuffer))
    return false;

  const unsigned char *der =
      derBuffer + sizeof(derBuffer) - static_cast<size_t>(derLength);
  unsigned char point[65] = {0};
  if (!extractP256PointFromSpki(der, static_cast<size_t>(derLength), point))
    return false;

  static constexpr char ALGORITHM[] = "ecdsa-sha2-nistp256";
  static constexpr char CURVE[] = "nistp256";
  unsigned char blob[OPENSSH_BLOB_BUFFER_SIZE] = {0};
  size_t blobLength = 0;
  if (!appendBytes(blob, sizeof(blob), blobLength,
                   reinterpret_cast<const unsigned char *>(ALGORITHM),
                   sizeof(ALGORITHM) - 1) ||
      !appendBytes(blob, sizeof(blob), blobLength,
                   reinterpret_cast<const unsigned char *>(CURVE),
                   sizeof(CURVE) - 1) ||
      !appendBytes(blob, sizeof(blob), blobLength, point, sizeof(point)))
    return false;

  unsigned char encoded[OPENSSH_BASE64_BUFFER_SIZE] = {0};
  size_t encodedLength = 0;
  if (mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &encodedLength, blob,
                            blobLength) != 0)
    return false;
  encoded[encodedLength] = '\0';

  publicKey = String(ALGORITHM) + " " +
              String(reinterpret_cast<const char *>(encoded));
  return true;
}

bool generateKeyPair(String &privateKey, String &publicKey) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctrDrbg;
  mbedtls_pk_context pk;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctrDrbg);
  mbedtls_pk_init(&pk);

  static constexpr char PERSONALIZATION[] = "esp32tun-ssh-p256";
  int result = mbedtls_ctr_drbg_seed(
      &ctrDrbg, mbedtls_entropy_func, &entropy,
      reinterpret_cast<const unsigned char *>(PERSONALIZATION),
      sizeof(PERSONALIZATION) - 1);
  if (result == 0)
    result = mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
  if (result == 0)
    result = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, mbedtls_pk_ec(pk),
                                 mbedtls_ctr_drbg_random, &ctrDrbg);

  unsigned char privatePem[PRIVATE_PEM_BUFFER_SIZE] = {0};
  if (result == 0)
    result = mbedtls_pk_write_key_pem(&pk, privatePem, sizeof(privatePem));
  if (result == 0)
    privateKey = String(reinterpret_cast<const char *>(privatePem));
  if (result == 0 && !buildOpenSshPublicKey(pk, publicKey))
    result = -1;

  mbedtls_pk_free(&pk);
  mbedtls_ctr_drbg_free(&ctrDrbg);
  mbedtls_entropy_free(&entropy);
  return result == 0 && !privateKey.isEmpty() && !publicKey.isEmpty();
}

bool persistKeyPair(const String &privateKey, const String &publicKey) {
  removeIfExists(KEY_TEMP);
  removeIfExists(PUBLIC_KEY_TEMP);
  if (!writeFile(KEY_TEMP, privateKey) || !writeFile(PUBLIC_KEY_TEMP, publicKey)) {
    removeIfExists(KEY_TEMP);
    removeIfExists(PUBLIC_KEY_TEMP);
    return false;
  }

  removeIfExists(KEY_PATH);
  removeIfExists(PUBLIC_KEY_PATH);
  if (!LittleFS.rename(KEY_TEMP, KEY_PATH)) {
    removeIfExists(KEY_TEMP);
    removeIfExists(PUBLIC_KEY_TEMP);
    return false;
  }
  if (!LittleFS.rename(PUBLIC_KEY_TEMP, PUBLIC_KEY_PATH)) {
    removeIfExists(KEY_PATH);
    removeIfExists(PUBLIC_KEY_TEMP);
    return false;
  }
  return true;
}
} // namespace

bool ensure(DeviceRuntimeConfig &config) {
  String privateKey;
  String publicKey;
  if (loadExisting(privateKey, publicKey)) {
    config.sshAuthMethod = SSHAuthMethod::PrivateKey;
    config.sshPrivateKey = privateKey;
    config.sshPublicKey = publicKey;
    config.sshKeyPassphrase = "";
    LOG_I("KEY", "Existing local ECDSA P-256 SSH identity loaded");
    return true;
  }

  LOG_I("KEY", "No local SSH identity found; generating ECDSA P-256 key pair");
  if (!generateKeyPair(privateKey, publicKey)) {
    LOG_E("KEY", "Unable to generate ECDSA P-256 SSH identity");
    return false;
  }
  if (!persistKeyPair(privateKey, publicKey)) {
    LOG_E("KEY", "Unable to store generated SSH identity");
    return false;
  }

  config.sshAuthMethod = SSHAuthMethod::PrivateKey;
  config.sshPrivateKey = privateKey;
  config.sshPublicKey = publicKey;
  config.sshKeyPassphrase = "";
  LOG_I("KEY", "Generated ECDSA P-256 SSH identity stored locally");
  return true;
}
} // namespace ssh_key_provisioning
