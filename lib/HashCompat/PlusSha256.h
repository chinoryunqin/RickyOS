#pragma once
#if defined(CROSSPOINT_EMULATED)
#include <cstddef>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
using PlusSha256Context = CC_SHA256_CTX;
static inline void plus_sha256_init(PlusSha256Context*) {}
static inline void plus_sha256_free(PlusSha256Context*) {}
static inline int plus_sha256_starts(PlusSha256Context* p, int is224) {
  return !is224 && CC_SHA256_Init(p) == 1 ? 0 : -1;
}
static inline int plus_sha256_update(PlusSha256Context* p, const unsigned char* b, size_t n) {
  return n <= UINT32_MAX && CC_SHA256_Update(p, b, static_cast<CC_LONG>(n)) == 1 ? 0 : -1;
}
static inline int plus_sha256_finish(PlusSha256Context* p, unsigned char out[32]) {
  return CC_SHA256_Final(out, p) == 1 ? 0 : -1;
}
#else
#include <openssl/sha.h>
using PlusSha256Context = SHA256_CTX;
static inline void plus_sha256_init(PlusSha256Context*) {}
static inline void plus_sha256_free(PlusSha256Context*) {}
static inline int plus_sha256_starts(PlusSha256Context* p, int is224) { return !is224 && SHA256_Init(p) == 1 ? 0 : -1; }
static inline int plus_sha256_update(PlusSha256Context* p, const unsigned char* b, size_t n) {
  return SHA256_Update(p, b, n) == 1 ? 0 : -1;
}
static inline int plus_sha256_finish(PlusSha256Context* p, unsigned char out[32]) {
  return SHA256_Final(out, p) == 1 ? 0 : -1;
}
#endif
#else
#include <esp_idf_version.h>
#if ESP_IDF_VERSION_MAJOR < 6
#include <mbedtls/sha256.h>
typedef mbedtls_sha256_context PlusSha256Context;
static inline void plus_sha256_init(PlusSha256Context* p) { mbedtls_sha256_init(p); }
static inline void plus_sha256_free(PlusSha256Context* p) { mbedtls_sha256_free(p); }
static inline int plus_sha256_starts(PlusSha256Context* p, int is224) { return mbedtls_sha256_starts(p, is224); }
static inline int plus_sha256_update(PlusSha256Context* p, const unsigned char* b, size_t n) {
  return mbedtls_sha256_update(p, b, n);
}
static inline int plus_sha256_finish(PlusSha256Context* p, unsigned char out[32]) {
  return mbedtls_sha256_finish(p, out);
}
#else
#pragma once
// IDF 6.1 / Mbed TLS 4 removed the public SHA-256 context API. Keep the
// existing RickyOS incremental OTA hashing contract through PSA Crypto.
#include <psa/crypto.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  psa_hash_operation_t operation;
} PlusSha256Context;
static inline void plus_sha256_init(PlusSha256Context* ctx) {
  const psa_hash_operation_t initial = PSA_HASH_OPERATION_INIT;
  ctx->operation = initial;
}
static inline void plus_sha256_free(PlusSha256Context* ctx) { (void)psa_hash_abort(&ctx->operation); }
static inline int plus_sha256_starts(PlusSha256Context* ctx, int is224) {
  psa_status_t status = psa_crypto_init();
  if (status != PSA_SUCCESS) return (int)status;
  status = psa_hash_abort(&ctx->operation);
  if (status != PSA_SUCCESS) return (int)status;
  return (int)psa_hash_setup(&ctx->operation, is224 ? PSA_ALG_SHA_224 : PSA_ALG_SHA_256);
}
static inline int plus_sha256_update(PlusSha256Context* ctx, const unsigned char* data, size_t len) {
  return (int)psa_hash_update(&ctx->operation, data, len);
}
static inline int plus_sha256_finish(PlusSha256Context* ctx, unsigned char output[32]) {
  size_t length = 0;
  return (int)psa_hash_finish(&ctx->operation, output, 32, &length);
}
#ifdef __cplusplus
}
#endif

#endif

#endif  // CROSSPOINT_EMULATED
