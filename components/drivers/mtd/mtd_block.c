/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * mtd_block: wrap rt_mtd_info as RT_Device_Class_Block.
 *
 * Write strategy: read-erase-write per erase block.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd.h>

#define DBG_TAG "mtd.block"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define MTD_BLOCK_SECTOR_SIZE  512

struct _mtd_block_dev
{
    struct rt_device               dev;
    struct rt_device_blk_geometry  geo;
    struct rt_mtd_info            *mtd;
    struct rt_mutex                lock;
    uint8_t                       *erase_buf;
};

static rt_err_t _blk_init(rt_device_t d)
{
    return RT_EOK;
}

static rt_err_t _blk_open(rt_device_t d, rt_uint16_t o)
{
    (void)o;
    return RT_EOK;
}

static rt_err_t _blk_close(rt_device_t d)
{
    return RT_EOK;
}

static rt_ssize_t _blk_read(rt_device_t dev, rt_off_t pos, void *buf, rt_size_t size)
{
    struct _mtd_block_dev *mbd = (struct _mtd_block_dev *)dev;
    loff_t off = (loff_t)pos * mbd->geo.bytes_per_sector;
    size_t len = (size_t)size * mbd->geo.bytes_per_sector;
    size_t retlen;
    int ret;

    rt_mutex_take(&mbd->lock, RT_WAITING_FOREVER);
    ret = rt_mtd_read(mbd->mtd, off, len, &retlen, (uint8_t *)buf);
    rt_mutex_release(&mbd->lock);

    return ret ? ret : (rt_ssize_t)(retlen / mbd->geo.bytes_per_sector);
}

static rt_ssize_t _blk_write(rt_device_t dev, rt_off_t pos, const void *buf, rt_size_t size)
{
    struct _mtd_block_dev *mbd = (struct _mtd_block_dev *)dev;
    uint32_t es = mbd->mtd->erasesize;
    loff_t off = (loff_t)pos * mbd->geo.bytes_per_sector;
    size_t len = (size_t)size * mbd->geo.bytes_per_sector;
    const uint8_t *src = buf;
    loff_t blk;

    if ((uint64_t)(off + len) > mbd->mtd->size)
        return -RT_EINVAL;

    rt_mutex_take(&mbd->lock, RT_WAITING_FOREVER);

    for (blk  = off & ~((loff_t)es - 1);
         blk < (off + len + es - 1) & ~((loff_t)es - 1);
         blk += es)
    {
        loff_t ws, we;
        size_t rl;

        if (rt_mtd_read(mbd->mtd, blk, es, &rl, mbd->erase_buf))
            rt_memset(mbd->erase_buf, 0xFF, es);

        ws = (off > blk) ? off : blk;
        we = (off + len < blk + es) ? (off + len) : (blk + es);
        rt_memcpy(mbd->erase_buf + (ws - blk), src + (ws - off),
                  (size_t)(we - ws));

        if (rt_mtd_erase(mbd->mtd, blk, es))
        {
            rt_mutex_release(&mbd->lock);
            return -RT_EIO;
        }
        if (rt_mtd_write(mbd->mtd, blk, es, &rl, mbd->erase_buf))
        {
            rt_mutex_release(&mbd->lock);
            return -RT_EIO;
        }
    }

    rt_mutex_release(&mbd->lock);
    return (rt_ssize_t)size;
}

static rt_err_t _blk_control(rt_device_t dev, int cmd, void *args)
{
    struct _mtd_block_dev *mbd = (struct _mtd_block_dev *)dev;
    rt_err_t ret = RT_EOK;

    rt_mutex_take(&mbd->lock, RT_WAITING_FOREVER);

    switch (cmd)
    {
    case RT_DEVICE_CTRL_BLK_GETGEOME:
        if (args)
            rt_memcpy(args, &mbd->geo, sizeof(mbd->geo));
        break;
    case RT_DEVICE_CTRL_BLK_ERASE:
        ret = rt_mtd_erase(mbd->mtd, 0, mbd->mtd->size);
        break;
    default:
        ret = -RT_EINVAL;
    }

    rt_mutex_release(&mbd->lock);
    return ret;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops _blk_ops =
{
    _blk_init,
    _blk_open,
    _blk_close,
    _blk_read,
    _blk_write,
    _blk_control,
};
#endif

struct rt_device *rt_mtd_block_create(const char *mtd_name, const char *blk_name)
{
    struct rt_mtd_info *mtd;
    struct _mtd_block_dev *mbd;

    mtd = (struct rt_mtd_info *)rt_device_find(mtd_name);
    if (!mtd)
        return RT_NULL;

    mbd = rt_malloc(sizeof(*mbd));
    if (!mbd)
        return RT_NULL;
    rt_memset(mbd, 0, sizeof(*mbd));

    mbd->erase_buf = rt_malloc(mtd->erasesize);
    if (!mbd->erase_buf)
    {
        rt_free(mbd);
        return RT_NULL;
    }

    mbd->mtd = mtd;
    rt_mutex_init(&mbd->lock, blk_name, RT_IPC_FLAG_PRIO);
    mbd->geo.bytes_per_sector = MTD_BLOCK_SECTOR_SIZE;
    mbd->geo.sector_count     = mtd->size / MTD_BLOCK_SECTOR_SIZE;
    mbd->geo.block_size       = mtd->erasesize;

    mbd->dev.type = RT_Device_Class_Block;
#ifdef RT_USING_DEVICE_OPS
    mbd->dev.ops = &_blk_ops;
#else
    mbd->dev.init    = _blk_init;
    mbd->dev.open    = _blk_open;
    mbd->dev.close   = _blk_close;
    mbd->dev.read    = _blk_read;
    mbd->dev.write   = _blk_write;
    mbd->dev.control = _blk_control;
#endif

    if (rt_device_register(&mbd->dev, blk_name, RT_DEVICE_FLAG_RDWR))
    {
        rt_mutex_detach(&mbd->lock);
        rt_free(mbd->erase_buf);
        rt_free(mbd);
        return RT_NULL;
    }

    LOG_I("mtdblock '%s' -> '%s' sectors=%lu",
          blk_name, mtd_name, (unsigned long)mbd->geo.sector_count);
    return &mbd->dev;
}

int rt_mtd_block_destroy(struct rt_device *blk)
{
    struct _mtd_block_dev *mbd = (struct _mtd_block_dev *)blk;

    if (!blk)
        return -RT_EINVAL;

    rt_device_unregister(blk);
    rt_mutex_detach(&mbd->lock);
    rt_free(mbd->erase_buf);
    rt_free(mbd);
    return RT_EOK;
}
