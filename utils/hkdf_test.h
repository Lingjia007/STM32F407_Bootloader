#ifndef HKDF_TEST_H
#define HKDF_TEST_H

#include <stdint.h>

void hkdf_key_derivation_test(const uint8_t *devkey, const uint8_t *uid, const uint8_t *dynamicsalt);
void run_test_vectors(void);

#endif
