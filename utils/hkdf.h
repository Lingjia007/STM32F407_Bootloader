#ifndef HKDF_H
#define HKDF_H

#include <stdint.h>
#include <stddef.h>

// heres some info :3

// derive keys with hkdf (rfc 5869)

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
