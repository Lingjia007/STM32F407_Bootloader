#ifndef HKDF_H
#define HKDF_H

#include <stdint.h>
#include <stddef.h>

// HKDF - HMAC-based Key Derivation Function (RFC 5869)
// Two-stage: Extract + Expand

// HKDF-Extract: PRK = HMAC-Hash(salt, IKM)
// salt: optional salt (NULL or length 0 allowed, will use zeros)
// ikm: input key material
// prk: output buffer (32 bytes for SHA256)
void hkdf_extract(const uint8_t *salt, size_t salt_len,
                  const uint8_t *ikm, size_t ikm_len,
                  uint8_t prk[32]);

// HKDF-Expand: OKM = HKDF-Expand(PRK, info, L)
// prk: pseudo-random key from hkdf_extract (32 bytes for SHA256)
// info: optional context info (can be NULL)
// okm: output buffer
// okm_len: desired length of output (max 255 * 32 = 8160 bytes)
void hkdf_expand(const uint8_t *prk, size_t prk_len,
                 const uint8_t *info, size_t info_len,
                 uint8_t *okm, size_t okm_len);

// HKDF: Combined Extract + Expand
// ikm: input key material
// salt: optional salt (NULL or length 0 allowed)
// info: optional context info
// okm: output buffer
// okm_len: desired length of output
void hkdf(const uint8_t *salt, size_t salt_len,
          const uint8_t *ikm, size_t ikm_len,
          const uint8_t *info, size_t info_len,
          uint8_t *okm, size_t okm_len);

#endif
