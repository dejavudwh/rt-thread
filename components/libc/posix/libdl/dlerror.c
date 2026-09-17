/*
 * Copyright (c) 2006-2024 RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author      Notes
 * 2010-11-17     yi.qiu      first version
 */

#include <rtthread.h>
#include <rtm.h>
#include <stdarg.h>

/* Last-error buffer shared by the dl*() API. The loader records a reason
 * string here on failure; dlerror() returns it and clears it (POSIX). */
static char _dl_error_buf[128];

/* record an error string for dlerror() to return later */
void dlmodule_seterror(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    rt_vsnprintf(_dl_error_buf, sizeof(_dl_error_buf), fmt, ap);
    va_end(ap);
}

/* clear any pending error (called after a successful load) */
void dlmodule_clearerror(void)
{
    _dl_error_buf[0] = '\0';
}

/**
 * @brief  retrieve a string describing the last error that occurred from a dynamic linking operation.
 *
 * @return const char* a string containing an error message describing the last error, or NULL if none.
 *
 * @note   The returned pointer stays valid until the next dl*() call. We do
 *         NOT clear the buffer on read, because POSIX requires the returned
 *         string to remain readable after dlerror() returns; the next failed
 *         load overwrites it, and a successful load clears it.
 */
const char *dlerror(void)
{
    if (_dl_error_buf[0] == '\0')
        return RT_NULL;

    return _dl_error_buf;
}
RTM_EXPORT(dlerror)
