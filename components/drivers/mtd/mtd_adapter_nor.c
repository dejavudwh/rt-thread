/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * NOR adapter — bridges old rt_mtd_nor_driver_ops to unified rt_mtd_ops.
 * Direct 1:1 forward, zero overhead.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd_nor.h>
#include <drivers/mtd.h>

#define DBG_TAG "mtd.adapter.nor"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

struct _nor_adapter
{
    struct rt_mtd_nor_device *dev;
    const struct rt_mtd_nor_driver_ops *ops;
};

static int _nor_read(struct rt_mtd_info *mtd, loff_t from, size_t len,
                     size_t *retlen, uint8_t *buf)
{
    struct _nor_adapter *a = mtd->priv;
    rt_ssize_t r;

    r = a->ops->read(a->dev, (rt_off_t)from, buf, len);
    if (r < 0)
    {
        if (retlen)
            *retlen = 0;
        return (int)r;
    }
    if (retlen)
        *retlen = (size_t)r;
    return 0;
}

static int _nor_write(struct rt_mtd_info *mtd, loff_t to, size_t len,
                      size_t *retlen, const uint8_t *buf)
{
    struct _nor_adapter *a = mtd->priv;
    rt_ssize_t r;

    r = a->ops->write(a->dev, (rt_off_t)to, buf, len);
    if (r < 0)
    {
        if (retlen)
            *retlen = 0;
        return (int)r;
    }
    if (retlen)
        *retlen = (size_t)r;
    return 0;
}

static int _nor_erase(struct rt_mtd_info *mtd, loff_t addr, size_t len)
{
    struct _nor_adapter *a = mtd->priv;

    return a->ops->erase_block(a->dev, (rt_off_t)addr, len) == RT_EOK ? 0 : -RT_EIO;
}

static const struct rt_mtd_ops _nor_ops =
{
    .read  = _nor_read,
    .write = _nor_write,
    .erase = _nor_erase,
};

int rt_mtd_nor_adapter_init(struct rt_mtd_info *mtd,
                            struct rt_mtd_nor_device *nor_dev,
                            const struct rt_mtd_nor_driver_ops *nor_ops)
{
    struct _nor_adapter *ad;

    RT_ASSERT(mtd && nor_dev && nor_ops);
    RT_ASSERT(nor_ops->read && nor_ops->write && nor_ops->erase_block);

    ad = rt_malloc(sizeof(*ad));
    if (!ad)
        return -RT_ENOMEM;

    ad->dev = nor_dev;
    ad->ops = nor_ops;

    rt_memset(mtd, 0, sizeof(*mtd));
    mtd->type      = RT_MTD_NORFLASH;
    mtd->flags     = RT_MTD_WRITEABLE;
    mtd->erasesize = nor_dev->block_size;
    mtd->writesize = 1;
    mtd->size      = (uint64_t)(nor_dev->block_end - nor_dev->block_start)
                     * nor_dev->block_size;
    mtd->ops       = &_nor_ops;
    mtd->priv      = ad;

    LOG_I("NOR: size=%lu es=%lu start=%lu end=%lu",
          (unsigned long)mtd->size, (unsigned long)mtd->erasesize,
          (unsigned long)nor_dev->block_start, (unsigned long)nor_dev->block_end);
    return RT_EOK;
}
