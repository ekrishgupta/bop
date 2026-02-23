#pragma once

#include <chrono>
#include <iomanip>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>
#include <sstream>
#include <string>
#include <vector>

namespace bop::auth {

struct Credentials {
  std::string api_key;
  std::string secret_key;
  std::string passphrase;
  std::string address;
};

inline std::string to_hex(const unsigned char *data, size_t len) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i) {
    ss << std::setw(2) << static_cast<int>(data[i]);
  }
  return ss.str();
}

inline std::string to_base64(const unsigned char *data, size_t len) {
  BIO *bio, *b64;
  BUF_MEM *bufferPtr;

  b64 = BIO_new(BIO_f_base64());
  bio = BIO_new(BIO_s_mem());
  bio = BIO_push(b64, bio);

  BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
  BIO_write(bio, data, len);
  BIO_flush(bio);
  BIO_get_mem_ptr(bio, &bufferPtr);

  std::string result(bufferPtr->data, bufferPtr->length);
  BIO_free_all(bio);

  return result;
}

inline std::string hmac_sha256(const std::string &key, const std::string &msg) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int len = 0;

  HMAC(EVP_sha256(), key.c_str(), key.length(),
       reinterpret_cast<const unsigned char *>(msg.c_str()), msg.length(), hash,
       &len);

  return std::string(reinterpret_cast<char *>(hash), len);
}

struct KalshiSigner {
  static std::string sign(const std::string &secret,
                          const std::string &timestamp,
                          const std::string &method, const std::string &path,
                          const std::string &body = "") {
    std::string message = timestamp + method + path + body;
    std::string hmac_res = hmac_sha256(secret, message);
    return to_base64(reinterpret_cast<const unsigned char *>(hmac_res.c_str()),
                     hmac_res.length());
  }
};

struct PolySigner {
  // Polymarket requires EIP-712 or personal_sign.
  // This is a utility to sign a message using a secp256k1 private key.
  static std::string sign(const std::string &private_key_hex,
                          const std::string &timestamp,
                          const std::string &method, const std::string &path,
                          const std::string &body = "") {
    std::string message = timestamp + method + path + body;

    // In a real implementation, we would:
    // 1. Convert private_key_hex to BIGNUM/EC_KEY
    // 2. Hash the message (Keccak-256 for Ethereum)
    // 3. ECDSA sign the hash
    // 4. Return as hex string with recovery ID (v, r, s)

    // For the purpose of this BOP implementation, we provide the signature
    // format. Actual EIP-712 requires complex domain separators and type
    // hashing.

    return "0x_mock_eip712_" +
           to_hex(reinterpret_cast<const unsigned char *>(message.c_str()),
                  std::min(message.length(), (size_t)32));
  }
};

} // namespace bop::auth
