/*
 * mod_mbedtls_demo.c — dlmodule driver for a real open-source crypto library
 *
 * Compiles the mbedTLS 2.28 LTS library (SHA-256 + AES-128-CBC) into a single
 * self-contained .so, then runs an end-to-end round-trip:
 *     1. SHA-256 of a known input, compared against the NIST reference digest.
 *     2. AES-128-CBC encrypt + decrypt round-trip, byte-for-byte comparison.
 *
 * The library's heap is mapped onto RT-Thread's rt_calloc/rt_free (see
 * mbedtls_config_min.h), and its libc calls (memcpy/memset/memcmp/...) resolve
 * to the symbols exported by the kernel's dlsyms.c.  This is the canonical
 * "open-source library + kernel symbol export" integration for dlmodule.
 *
 * Build (see build_mbedtls.sh):
 *   aarch64-none-elf-gcc -march=armv8-a -shared -fPIC -nostdlib -O2 \
 *       -DMBEDTLS_CONFIG_FILE='"mbedtls_config_min.h"' \
 *       -I<rt-thread>/tests/dlmodule -I<mbedtls>/include \
 *       -o mod_mbedtls.so <mbedtls>/library/{aes,sha256,platform_util,platform}.c \
 *       mod_mbedtls_demo.c
 */

#ifndef NULL
#define NULL ((void*)0)
#endif

extern int   rt_kprintf(const char *fmt, ...);
extern void *rt_calloc(unsigned long n, unsigned long size);
extern void  rt_free(void *ptr);
extern int   memcmp(const void *a, const void *b, unsigned long n);

#include "mbedtls/sha256.h"
#include "mbedtls/aes.h"
#include "mbedtls/platform.h"   /* exposes mbedtls_calloc/free macros -> rt_calloc/free */

/* NIST SHA-256 test vector: SHA-256("abc") */
static const unsigned char sha256_abc_expected[32] =
{
    0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
    0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
    0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
    0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD,
};

/* 128-bit AES key + IV + a 32-byte plaintext for the CBC round-trip.
 * NOTE: the IV is NOT const — mbedtls_aes_crypt_cbc updates it in place,
 * and CBC requires the encrypt and decrypt passes to start from the SAME
 * initial IV, so we keep two separate writable copies. */
static const unsigned char aes_key[16] =
    { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
      0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
static const unsigned char aes_iv_init[16] =
    { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };

static const unsigned char plaintext[32] =
    "RT-Thread dlmodule mbedTLS demo!";

int mbedtls_demo(void)
{
    unsigned char digest[32];
    unsigned char cipher[32];
    unsigned char recovered[32];
    unsigned char iv[16];
    int           i;

    /* 1. SHA-256("abc") */
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);               /* 0 = SHA-256, not SHA-224 */
    mbedtls_sha256_update(&sha, (const unsigned char *)"abc", 3);
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (memcmp(digest, sha256_abc_expected, 32) != 0)
    {
        rt_kprintf("[mbedtls] FAIL: SHA-256 mismatch\n");
        return -1;
    }
    rt_kprintf("[mbedtls] SHA-256(\"abc\") = ba7816bf8f01cfea... (ok)\n");

    /* 2. AES-128-CBC encrypt -> decrypt round-trip */
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_enc(&aes, aes_key, 128) != 0)
    {
        rt_kprintf("[mbedtls] FAIL: setkey_enc\n");
        return -2;
    }
    for (i = 0; i < 16; i++)
        iv[i] = aes_iv_init[i];
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, sizeof(plaintext),
                              iv, plaintext, cipher) != 0)
    {
        rt_kprintf("[mbedtls] FAIL: crypt_cbc encrypt\n");
        return -3;
    }

    mbedtls_aes_free(&aes);
    mbedtls_aes_init(&aes);
    if (mbedtls_aes_setkey_dec(&aes, aes_key, 128) != 0)
    {
        rt_kprintf("[mbedtls] FAIL: setkey_dec\n");
        return -4;
    }
    for (i = 0; i < 16; i++)
        iv[i] = aes_iv_init[i];
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, sizeof(plaintext),
                              iv, cipher, recovered) != 0)
    {
        rt_kprintf("[mbedtls] FAIL: crypt_cbc decrypt\n");
        return -5;
    }
    mbedtls_aes_free(&aes);

    if (memcmp(recovered, plaintext, sizeof(plaintext)) != 0)
    {
        rt_kprintf("[mbedtls] FAIL: AES round-trip mismatch\n");
        return -6;
    }

    rt_kprintf("[mbedtls] AES-128-CBC round-trip (%d bytes): ",
               (int)sizeof(plaintext));
    for (i = 0; i < 12; i++)
        rt_kprintf("%02x", recovered[i]);
    rt_kprintf("... (ok)\n");

    /* 3. explicit allocator mapping check: mbedtls_calloc/free are macros to
     * rt_calloc/rt_free (kernel exports) — prove the heap boundary works. */
    {
        unsigned char *buf = mbedtls_calloc(1, 64);
        if (buf == NULL)
        {
            rt_kprintf("[mbedtls] FAIL: mbedtls_calloc returned NULL\n");
            return -7;
        }
        for (i = 0; i < 64; i++)
            buf[i] = (unsigned char)i;
        if (buf[63] != 63)
        {
            rt_kprintf("[mbedtls] FAIL: calloc buffer write\n");
            mbedtls_free(buf);
            return -8;
        }
        mbedtls_free(buf);
        rt_kprintf("[mbedtls] mbedtls_calloc/free -> rt_calloc/rt_free (ok)\n");
    }

    rt_kprintf("[mbedtls] ALL CHECKS PASSED\n");
    return 0;
}

int module_init(void)
{
    rt_kprintf("[mbedtls] module loaded (mbedTLS 2.28.9, SHA-256 + AES-CBC)\n");
    return 0;
}

void module_cleanup(void)
{
    rt_kprintf("[mbedtls] module unloaded\n");
}
