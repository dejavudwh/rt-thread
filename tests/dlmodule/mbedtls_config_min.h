/*
 * mbedtls_config_min.h — minimal mbedTLS configuration for the dlmodule demo
 *
 * Enables ONLY what mod_mbedtls_demo.c needs (SHA-256 + AES-128-CBC) and routes
 * all heap allocations to RT-Thread's rt_calloc/rt_free so the .so has no libc
 * heap dependency.  Everything else is left disabled.
 *
 * Usage: build with
 *   -DMBEDTLS_CONFIG_FILE='"mbedtls_config_min.h"'
 * and add this directory to the include path.
 */

#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/* Declare the RT-Thread allocator up front so the platform.h macros below
 * have a prototype in every mbedTLS translation unit. */
#include <stddef.h>
extern void *rt_calloc(size_t n, size_t size);
extern void  rt_free(void *ptr);

/* Crypto primitives */
#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_SHA256_C

/* Platform glue: route mbedtls_calloc/free to rt_calloc/rt_free */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_CALLOC_MACRO   rt_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO     rt_free

#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
