#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "hkdf.h"

static void print_hex(const char *label, const uint8_t *data, size_t len)
{
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

static void print_separator(void)
{
    printf("--------------------------------------------------------------------------\n");
}

void hkdf_key_derivation_test(const uint8_t *devkey, const uint8_t *uid, const uint8_t *dynamicsalt)
{
    uint8_t prk[32];
    uint8_t aes_key[32];
    
    printf("\n");
    printf("==========================================================================\n");
    printf("Two-Stage HKDF Key Derivation Test\n");
    printf("==========================================================================\n");
    
    print_separator();
    printf("[Input Parameters]\n");
    print_separator();
    print_hex("DevKey (OTP, 128-bit)   ", devkey, 16);
    print_hex("UID (Chip ID, 96-bit)   ", uid, 12);
    print_hex("DynamicSalt (128-bit)   ", dynamicsalt, 16);
    
    print_separator();
    printf("[Derivation Process]\n");
    print_separator();
    
    printf("Stage 1 - HKDF-Extract:\n");
    printf("  PRK = HMAC-SHA256(DynamicSalt, DevKey)\n");
    
    hkdf_extract(dynamicsalt, 16, devkey, 16, prk);
    print_hex("  PRK", prk, 32);
    
    printf("Stage 2 - HKDF-Expand:\n");
    printf("  AES_Key = HKDF-Expand(PRK, info=UID, len=32)\n");
    
    hkdf_expand(prk, 32, uid, 12, aes_key, 32);
    print_hex("  AES_Key", aes_key, 32);
    
    print_separator();
    print_hex("Derived Key (AES-256)", aes_key, 32);
    printf("==========================================================================\n\n");
}

void run_test_vectors(void)
{
    printf("\n");
    printf("**************************************************************************\n");
    printf("*                    HKDF Test Vectors for Verification                 *\n");
    printf("**************************************************************************\n");
    
    {
        printf("\n[Test Vector 1 - Random Values]\n");
        uint8_t devkey[16] = {
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
            0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00
        };
        uint8_t uid[12] = {
            0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
            0x11, 0x22, 0x33, 0x44
        };
        uint8_t dynamicsalt[16] = {
            0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
            0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF
        };
        hkdf_key_derivation_test(devkey, uid, dynamicsalt);
    }
    
    {
        printf("\n[Test Vector 2 - All Zeros]\n");
        uint8_t devkey[16] = {0};
        uint8_t uid[12] = {0};
        uint8_t dynamicsalt[16] = {0};
        hkdf_key_derivation_test(devkey, uid, dynamicsalt);
    }
    
    {
        printf("\n[Test Vector 3 - Sequential]\n");
        uint8_t devkey[16];
        uint8_t uid[12];
        uint8_t dynamicsalt[16];
        for (int i = 0; i < 16; i++) devkey[i] = (uint8_t)(i + 1);
        for (int i = 0; i < 12; i++) uid[i] = (uint8_t)(i + 0x10);
        for (int i = 0; i < 16; i++) dynamicsalt[i] = (uint8_t)(i + 0x20);
        hkdf_key_derivation_test(devkey, uid, dynamicsalt);
    }
    
    printf("**************************************************************************\n");
    printf("*                    Compare with Python Host Computer                   *\n");
    printf("**************************************************************************\n");
    printf("Run the Python host computer with the same DevKey, UID, and DynamicSalt\n");
    printf("values to verify that the derived AES-256 keys match exactly.\n\n");
}

int main(int argc, char *argv[])
{
    if (argc == 4) {
        uint8_t devkey[16];
        uint8_t uid[12];
        uint8_t dynamicsalt[16];
        
        if (strlen(argv[1]) != 32 || strlen(argv[2]) != 24 || strlen(argv[3]) != 32) {
            fprintf(stderr, "Error: Invalid hex string length\n");
            fprintf(stderr, "Usage: %s <DevKey(32hex)> <UID(24hex)> <DynamicSalt(32hex)>\n", argv[0]);
            fprintf(stderr, "   or: %s  (run built-in test vectors)\n", argv[0]);
            return 1;
        }
        
        for (int i = 0; i < 16; i++) {
            sscanf(argv[1] + i*2, "%2hhx", &devkey[i]);
        }
        for (int i = 0; i < 12; i++) {
            sscanf(argv[2] + i*2, "%2hhx", &uid[i]);
        }
        for (int i = 0; i < 16; i++) {
            sscanf(argv[3] + i*2, "%2hhx", &dynamicsalt[i]);
        }
        
        hkdf_key_derivation_test(devkey, uid, dynamicsalt);
    } else {
        run_test_vectors();
    }
    
    return 0;
}
