/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * mtd_char: registers an mtd_info as RT_Device_Class_MTD in the
 * RT-Thread device tree. Provides rt_device_read/write/control
 * wrappers around mtd_core APIs.
 *
 * This is the ONLY entry point that makes an mtd_info visible
 * to rt_device_find(). Masters and partitions both use this path.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd.h>

#define DBG_TAG "mtd.char"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* internal helpers from mtd_core.c */
extern void _mtd_core_table_add(struct rt_mtd_info *mtd);
extern void _mtd_core_table_remove(struct rt_mtd_info *mtd);

static rt_err_t _mtd_init(rt_device_t dev)  { return RT_EOK; }
static rt_err_t _mtd_open(rt_device_t dev, rt_uint16_t o) { (void)o; return RT_EOK; }
static rt_err_t _mtd_close(rt_device_t dev) { return RT_EOK; }

static rt_ssize_t _mtd_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    struct rt_mtd_info *mtd = (struct rt_mtd_info *)dev;
    size_t retlen;
    int ret;

    ret = rt_mtd_read(mtd, (loff_t)pos, size, &retlen, (uint8_t *)buf);
    return ret ? (rt_ssize_t)ret : (rt_ssize_t)retlen;
}

static rt_ssize_t _mtd_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    struct rt_mtd_info *mtd = (struct rt_mtd_info *)dev;
    size_t retlen;
    int ret;

    ret = rt_mtd_write(mtd, (loff_t)pos, size, &retlen, (const uint8_t *)buf);
    return ret ? (rt_ssize_t)ret : (rt_ssize_t)retlen;
}

static rt_err_t _mtd_control(rt_device_t dev, int cmd, void *args)
{
    struct rt_mtd_info *mtd = (struct rt_mtd_info *)dev;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_MTD_FORMAT:
        return rt_mtd_erase(mtd, 0, (size_t)mtd->size);

    case RT_DEVICE_CTRL_MTD_GET_INFO:
        if (!args) return -RT_EINVAL;
        rt_memcpy(args, mtd, sizeof(*mtd));
        return RT_EOK;

    case RT_DEVICE_CTRL_MTD_ERASE:
        return rt_mtd_erase(mtd, 0, (size_t)mtd->size);

    case RT_DEVICE_CTRL_MTD_IS_BAD:
    {
        loff_t *ofs = (loff_t *)args;
        int r;
        if (!args) return -RT_EINVAL;
        r = rt_mtd_block_isbad(mtd, *ofs);
        return r > 0 ? 1 : 0;
    }

    case RT_DEVICE_CTRL_MTD_MARK_BAD:
    {
        loff_t *ofs = (loff_t *)args;
        return args ? rt_mtd_block_markbad(mtd, *ofs) : -RT_EINVAL;
    }

    default:
        return -RT_EINVAL;
    }
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops _char_ops =
{
    _mtd_init, _mtd_open, _mtd_close,
    _mtd_read, _mtd_write, _mtd_control,
};
#endif

int rt_mtd_char_register(struct rt_mtd_info *mtd, const char *name)
{
    rt_device_t dev = &mtd->parent;
    int ret;

    RT_ASSERT(mtd && name);

    dev->type = RT_Device_Class_MTD;
#ifdef RT_USING_DEVICE_OPS
    dev->ops = &_char_ops;
#else
    dev->init    = _mtd_init;
    dev->open    = _mtd_open;
    dev->close   = _mtd_close;
    dev->read    = _mtd_read;
    dev->write   = _mtd_write;
    dev->control = _mtd_control;
#endif
    dev->rx_indicate = RT_NULL;
    dev->tx_complete = RT_NULL;

    ret = rt_device_register(dev, name, RT_DEVICE_FLAG_RDWR);
    if (ret)
    {
        LOG_E("register '%s' failed", name);
        return ret;
    }

    /* also track it in mtd_core's internal table */
    _mtd_core_table_add(mtd);

    LOG_I("'%s': type=%d size=%lu erasesize=%lu",
          name, mtd->type, (unsigned long)mtd->size, (unsigned long)mtd->erasesize);
    return RT_EOK;
}

int rt_mtd_char_unregister(struct rt_mtd_info *mtd)
{
    RT_ASSERT(mtd);
    _mtd_core_table_remove(mtd);
    return rt_device_unregister(&mtd->parent);
}
