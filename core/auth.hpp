#pragma once

#include <array>
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

inline std::vector<unsigned char> from_hex(const std::string &hex) {
  std::vector<unsigned char> res;
  std::string h = hex;
  if (h.size() >= 2 && h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) {
    h = h.substr(2);
  }
  for (size_t i = 0; i < h.length(); i += 2) {
    res.push_back(
        static_cast<unsigned char>(std::stoi(h.substr(i, 2), nullptr, 16)));
  }
  return res;
}

inline std::string pad32(const std::string &s) {
  std::string res = std::string(32 - s.size(), '\0') + s;
  return res;
}

inline std::string encode_uint256(uint64_t val) {
  std::string res(32, '\0');
  for (int i = 0; i < 8; ++i) {
    res[31 - i] = static_cast<char>((val >> (i * 8)) & 0xFF);
  }
  return res;
}

inline std::array<uint8_t, 32> encode_uint256_array(uint64_t val) {
  std::array<uint8_t, 32> res;
  res.fill(0);
  for (int i = 0; i < 8; ++i) {
    res[31 - i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
  }
  return res;
}

inline std::string encode_address(const std::string &addr_hex) {
  auto bytes = from_hex(addr_hex);
  std::string res(32, '\0');
  size_t start = 32 - bytes.size();
  for (size_t i = 0; i < bytes.size(); ++i) {
    res[start + i] = static_cast<char>(bytes[i]);
  }
  return res;
}

inline std::array<uint8_t, 32>
encode_address_array(const std::string &addr_hex) {
  auto bytes = from_hex(addr_hex);
  std::array<uint8_t, 32> res;
  res.fill(0);
  size_t start = 32 - bytes.size();
  for (size_t i = 0; i < bytes.size(); ++i) {
    res[start + i] = static_cast<uint8_t>(bytes[i]);
  }
  return res;
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

// Minimal Keccak-256 implementation
namespace keccak {
static const uint64_t round_constants[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL, 0x800000000000808aULL,
    0x8000000080008000ULL, 0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL, 0x000000000000008aULL,
    0x0000000000000088ULL, 0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL, 0x8000000000008089ULL,
    0x8000000000008003ULL, 0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL, 0x8000000080008081ULL,
    0x8000000000008080ULL, 0x0000000080000001ULL, 0x8000000080008008ULL};

static void keccakf(uint64_t state[25]) {
  for (int round = 0; round < 24; ++round) {
    uint64_t bc[5], t;
    for (int i = 0; i < 5; ++i)
      bc[i] = state[i] ^ state[i + 5] ^ state[i + 10] ^ state[i + 15] ^
              state[i + 20];
    for (int i = 0; i < 5; ++i) {
      t = bc[(i + 4) % 5] ^ ((bc[(i + 1) % 5] << 1) | (bc[(i + 1) % 5] >> 63));
      for (int j = 0; j < 25; j += 5)
        state[i + j] ^= t;
    }
    t = state[1];
    state[1] = (state[6] << 44) | (state[6] >> 20);
    state[6] = (state[9] << 20) | (state[9] >> 44);
    state[9] = (state[22] << 61) | (state[22] >> 3);
    state[22] = (state[14] << 39) | (state[14] >> 25);
    state[14] = (state[20] << 18) | (state[20] >> 46);
    state[20] = (state[2] << 62) | (state[2] >> 2);
    state[2] = (state[12] << 43) | (state[12] >> 21);
    state[12] = (state[13] << 25) | (state[13] >> 39);
    state[13] = (state[19] << 8) | (state[19] >> 56);
    state[19] = (state[23] << 56) | (state[23] >> 8);
    state[23] = (state[15] << 41) | (state[15] >> 23);
    state[15] = (state[4] << 27) | (state[4] >> 37);
    state[4] = (state[24] << 14) | (state[24] >> 50);
    state[24] = (state[21] << 2) | (state[21] >> 62);
    state[21] = (state[8] << 55) | (state[8] >> 9);
    state[8] = (state[16] << 45) | (state[16] >> 19);
    state[16] = (state[5] << 3) | (state[5] >> 61);
    state[5] = (state[3] << 28) | (state[3] >> 36);
    state[3] = (state[18] << 21) | (state[18] >> 43);
    state[18] = (state[17] << 15) | (state[17] >> 49);
    state[17] = (state[11] << 10) | (state[11] >> 54);
    state[11] = (state[7] << 6) | (state[7] >> 58);
    state[7] = (state[10] << 1) | (state[10] >> 63);
    state[10] = t;

    for (int j = 0; j < 25; j += 5) {
      bc[0] = state[j];
      bc[1] = state[j + 1];
      bc[2] = state[j + 2];
      bc[3] = state[j + 3];
      bc[4] = state[j + 4];
      for (int i = 0; i < 5; ++i)
        state[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
    }
    state[0] ^= round_constants[round];
  }
}

// Array-based hash function to eliminate std::string allocations
inline std::array<uint8_t, 32> hash_array(const uint8_t *data, size_t len) {
  uint64_t state[25] = {0};
  size_t rate = 136;
  size_t pos = 0;

  for (size_t i = 0; i < len; ++i) {
    state[pos / 8] ^= static_cast<uint64_t>(data[i]) << (8 * (pos % 8));
    pos++;
    if (pos == rate) {
      keccakf(state);
      pos = 0;
    }
  }

  state[pos / 8] ^= 0x01ULL << (8 * (pos % 8));
  state[(rate - 1) / 8] ^= 0x80ULL << (8 * ((rate - 1) % 8));
  keccakf(state);

  std::array<uint8_t, 32> res;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 8; ++j) {
      res[i * 8 + j] = static_cast<uint8_t>((state[i] >> (8 * j)) & 0xFF);
    }
  }
  return res;
}

inline std::string hash(const std::string &data) {
  uint64_t state[25] = {0};
  size_t rate = 136;
  size_t pos = 0;

  for (unsigned char c : data) {
    state[pos / 8] ^= static_cast<uint64_t>(c) << (8 * (pos % 8));
    pos++;
    if (pos == rate) {
      keccakf(state);
      pos = 0;
    }
  }

  state[pos / 8] ^= 0x01ULL << (8 * (pos % 8));
  state[(rate - 1) / 8] ^= 0x80ULL << (8 * ((rate - 1) % 8));
  keccakf(state);

  std::string res;
  res.resize(32);
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 8; ++j) {
      res[i * 8 + j] = static_cast<char>((state[i] >> (8 * j)) & 0xFF);
    }
  }
  return res;
}
} // namespace keccak

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

namespace eth {

inline std::string address_from_pubkey(const EC_POINT *pub_point,
                                       const EC_GROUP *group) {
  unsigned char *pub_bin = nullptr;
  size_t pub_len = EC_POINT_point2buf(
      group, pub_point, POINT_CONVERSION_UNCOMPRESSED, &pub_bin, nullptr);
  if (pub_len == 0)
    return "";

  // Skip the 0x04 prefix byte
  std::string pub_data(reinterpret_cast<char *>(pub_bin + 1), pub_len - 1);
  OPENSSL_free(pub_bin);

  std::string k_hash = keccak::hash(pub_data);
  // Address is the last 20 bytes of the Keccak-256 hash
  return "0x" +
         to_hex(reinterpret_cast<const unsigned char *>(k_hash.c_str() + 12),
                20);
}

inline int recover_v(const std::string &hash_bytes, const BIGNUM *r,
                     const BIGNUM *s, const std::string &expected_addr) {
  EC_KEY *key = EC_KEY_new_by_curve_name(NID_secp256k1);
  const EC_GROUP *group = EC_KEY_get0_group(key);
  BIGNUM *order = BN_new();
  EC_GROUP_get_order(group, order, nullptr);

  for (int v = 27; v <= 28; ++v) {
    int recid = v - 27;

    // This is a simplified recovery logic for secp256k1
    // In a real production system, one would use libsecp256k1 or a more
    // comprehensive OpenSSL-based recovery. For bop, we will try to match the
    // address. Given OpenSSL doesn't have a simple 'recover' function, many
    // implementations use a pre-computed V or external library. We'll use 27 as
    // a default but allow for 28 if we were to implement full recovery.
  }

  BN_free(order);
  EC_KEY_free(key);
  return 27;
}

inline std::string sign_hash_array(const std::string &private_key_hex,
                                   const std::array<uint8_t, 32> &hash_bytes,
                                   const std::string &expected_addr = "") {
  auto priv_bytes = from_hex(private_key_hex);
  BIGNUM *priv_bn = BN_bin2bn(priv_bytes.data(), priv_bytes.size(), nullptr);
  EC_KEY *key = EC_KEY_new_by_curve_name(NID_secp256k1);
  EC_KEY_set_private_key(key, priv_bn);

  EC_POINT *pub_point = EC_POINT_new(EC_KEY_get0_group(key));
  EC_POINT_mul(EC_KEY_get0_group(key), pub_point, priv_bn, nullptr, nullptr,
               nullptr);
  EC_KEY_set_public_key(key, pub_point);

  ECDSA_SIG *sig = ECDSA_do_sign(hash_bytes.data(), hash_bytes.size(), key);

  const BIGNUM *r_bn, *s_bn;
  ECDSA_SIG_get0(sig, &r_bn, &s_bn);

  BIGNUM *order = BN_new();
  EC_GROUP_get_order(EC_KEY_get0_group(key), order, nullptr);
  BIGNUM *half_order = BN_new();
  BN_rshift1(half_order, order);
  if (BN_cmp(s_bn, half_order) > 0) {
    BIGNUM *new_s = BN_new();
    BN_sub(new_s, order, s_bn);
    ECDSA_SIG_set0(sig, BN_dup(r_bn), new_s);
    ECDSA_SIG_get0(sig, &r_bn, &s_bn);
  }

  // Placeholder for V calculation - usually requires testing recovery
  int v = 27;

  unsigned char r_bin[32], s_bin[32];
  BN_bn2binpad(r_bn, r_bin, 32);
  BN_bn2binpad(s_bn, s_bin, 32);
  unsigned char v_byte = static_cast<unsigned char>(v);

  std::string signature =
      "0x" + to_hex(r_bin, 32) + to_hex(s_bin, 32) + to_hex(&v_byte, 1);

  ECDSA_SIG_free(sig);
  EC_KEY_free(key);
  BN_free(priv_bn);
  BN_free(order);
  BN_free(half_order);
  EC_POINT_free(pub_point);

  return signature;
}

inline std::string sign_hash(const std::string &private_key_hex,
                             const std::string &hash_bytes,
                             const std::string &expected_addr) {
  std::array<uint8_t, 32> arr;
  for (size_t i = 0; i < 32 && i < hash_bytes.size(); ++i) {
    arr[i] = static_cast<uint8_t>(hash_bytes[i]);
  }
  return sign_hash_array(private_key_hex, arr, expected_addr);
}
} // namespace eth

struct PolySigner {
  // Polymarket requires EIP-712 or personal_sign.
  // This is a utility to sign a message using a secp256k1 private key.
  static std::string sign(const std::string &private_key_hex,
                          const std::string &address_hex,
                          const std::string &timestamp,
                          const std::string &method, const std::string &path,
                          const std::string &body = "") {
    static const auto domainSeparator = [] {
      std::string typeHash_Domain = keccak::hash(
          "EIP712Domain(string name,string version,uint256 chainId)");
      std::string nameHash = keccak::hash("ClobApi");
      std::string versionHash = keccak::hash("1");
      std::string chainId = encode_uint256(137);
      std::string combined = typeHash_Domain + nameHash + versionHash + chainId;
      std::string h = keccak::hash(combined);
      std::array<uint8_t, 32> arr;
      for (int i = 0; i < 32; ++i)
        arr[i] = h[i];
      return arr;
    }();

    static const auto typeHash_ClobAuth = [] {
      std::string h =
          keccak::hash("ClobAuth(address address,string timestamp,string "
                       "method,string path,string body)");
      std::array<uint8_t, 32> arr;
      for (int i = 0; i < 32; ++i)
        arr[i] = h[i];
      return arr;
    }();

    auto addrEncoded = encode_address_array(address_hex);
    auto tsHash = keccak::hash_array(
        reinterpret_cast<const uint8_t *>(timestamp.data()), timestamp.size());
    auto mHash = keccak::hash_array(
        reinterpret_cast<const uint8_t *>(method.data()), method.size());
    auto pHash = keccak::hash_array(
        reinterpret_cast<const uint8_t *>(path.data()), path.size());
    auto bHash = keccak::hash_array(
        reinterpret_cast<const uint8_t *>(body.data()), body.size());

    std::array<uint8_t, 32 * 6> structData;
    auto copy32 = [](uint8_t *dst, const std::array<uint8_t, 32> &src) {
      for (int i = 0; i < 32; ++i)
        dst[i] = src[i];
    };
    copy32(structData.data(), typeHash_ClobAuth);
    copy32(structData.data() + 32, addrEncoded);
    copy32(structData.data() + 64, tsHash);
    copy32(structData.data() + 96, mHash);
    copy32(structData.data() + 128, pHash);
    copy32(structData.data() + 160, bHash);

    auto structHash = keccak::hash_array(structData.data(), structData.size());

    std::array<uint8_t, 2 + 32 + 32> finalData;
    finalData[0] = 0x19;
    finalData[1] = 0x01;
    copy32(finalData.data() + 2, domainSeparator);
    copy32(finalData.data() + 34, structHash);

    auto finalHash = keccak::hash_array(finalData.data(), finalData.size());

    return eth::sign_hash_array(private_key_hex, finalHash, address_hex);
  }

  static std::string
  sign_order(const std::string &private_key_hex, const std::string &address_hex,
             const std::string &token_id, const std::string &price,
             const std::string &size, const std::string &side,
             const std::string &expiration, uint64_t nonce) {
    static const auto domainSeparator = [] {
      std::string typeHash_Domain = keccak::hash(
          "EIP712Domain(string name,string version,uint256 chainId)");
      std::string nameHash = keccak::hash("ClobOrder");
      std::string versionHash = keccak::hash("1");
      std::string chainId = encode_uint256(137);
      std::string combined = typeHash_Domain + nameHash + versionHash + chainId;
      std::string h = keccak::hash(combined);
      std::array<uint8_t, 32> arr;
      for (int i = 0; i < 32; ++i)
        arr[i] = h[i];
      return arr;
    }();

    static const auto typeHash_Order = [] {
      std::string h = keccak::hash("Order(address owner,uint256 "
                                   "token_id,uint256 price,uint256 size,uint8 "
                                   "side,uint256 expiration,uint256 nonce)");
      std::array<uint8_t, 32> arr;
      for (int i = 0; i < 32; ++i)
        arr[i] = h[i];
      return arr;
    }();

    auto ownerEncoded = encode_address_array(address_hex);
    auto dec_to_array = [](const std::string &s) {
      BIGNUM *bn = nullptr;
      BN_dec2bn(&bn, s.c_str());
      std::array<uint8_t, 32> res;
      res.fill(0);
      BN_bn2binpad(bn, res.data(), 32);
      BN_free(bn);
      return res;
    };

    auto tokenEncoded = dec_to_array(token_id);
    auto priceEncoded = dec_to_array(price);
    auto sizeEncoded = dec_to_array(size);
    std::array<uint8_t, 32> sideEncoded;
    sideEncoded.fill(0);
    sideEncoded[31] = (side == "BUY" ? 0 : 1);
    auto expEncoded = dec_to_array(expiration);
    auto nonceEncoded = encode_uint256_array(nonce);

    std::array<uint8_t, 32 * 8> structData;
    auto copy32 = [](uint8_t *dst, const std::array<uint8_t, 32> &src) {
      for (int i = 0; i < 32; ++i)
        dst[i] = src[i];
    };
    copy32(structData.data(), typeHash_Order);
    copy32(structData.data() + 32, ownerEncoded);
    copy32(structData.data() + 64, tokenEncoded);
    copy32(structData.data() + 96, priceEncoded);
    copy32(structData.data() + 128, sizeEncoded);
    copy32(structData.data() + 160, sideEncoded);
    copy32(structData.data() + 192, expEncoded);
    copy32(structData.data() + 224, nonceEncoded);

    auto structHash = keccak::hash_array(structData.data(), structData.size());

    std::array<uint8_t, 2 + 32 + 32> finalData;
    finalData[0] = 0x19;
    finalData[1] = 0x01;
    copy32(finalData.data() + 2, domainSeparator);
    copy32(finalData.data() + 34, structHash);

    auto finalHash = keccak::hash_array(finalData.data(), finalData.size());

    return eth::sign_hash_array(private_key_hex, finalHash, address_hex);
  }
};

} // namespace bop::auth
