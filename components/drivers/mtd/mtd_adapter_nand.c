/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * NAND adapter — bridges old rt_mtd_nand_driver_ops to unified rt_mtd_ops.
 *
 * Read:  full page into page_buf for ECC, then memcpy to caller.
 *        Any byte range is supported (NAND requires full-page ECC).
 * Write: core enforces writesize alignment, full-page writes only.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd_nand.h>
#include <drivers/mtd.h>

#define DBG_TAG "mtd.adapter.nand"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

struct _nand_adapter
{
    struct rt_mtd_nand_device *dev;
    const struct rt_mtd_nand_driver_ops *ops;
    uint32_t block_size;
    uint8_t *page_buf;  /* full-page buffer for ECC reads */
    uint8_t *oob_buf;   /* full-OOB buffer for ECC OOB reads */
};

static inline uint32_t _page(struct _nand_adapter *a, loff_t off)
{
    return (uint32_t)((uint64_t)off / a->dev->page_size);
}

static inline uint32_t _block(struct _nand_adapter *a, loff_t off)
{
    return (uint32_t)((uint64_t)off / a->block_size);
}

static int _nand_read(struct rt_mtd_info *mtd, loff_t from, size_t len,
                      size_t *retlen, uint8_t *buf)
{
    struct _nand_adapter *a = mtd->priv;
    uint32_t ps = a->dev->page_size;
    uint8_t *pb = a->page_buf;
    size_t total = 0;

    while (len > 0)
    {
        uint32_t page  = _page(a, from);
        uint32_t col   = (uint32_t)((uint64_t)from % ps);
        uint32_t chunk = ps - col;
        int ret;

        if (chunk > len)
            chunk = (uint32_t)len;

        /* read full page for ECC, then copy requested range */
        ret = a->ops->read_page(a->dev, page, pb, ps, RT_NULL, 0);
        if (ret != RT_MTD_EOK && ret != RT_MTD_EECC_CORRECT)
        {
            if (retlen)
                *retlen = total;
            return -RT_EIO;
        }

        rt_memcpy(buf, pb + col, chunk);
        total += chunk;
        buf   += chunk;
        from  += chunk;
        len   -= chunk;
    }

    if (retlen)
        *retlen = total;
    return 0;
}

static int _nand_write(struct rt_mtd_info *mtd, loff_t to, size_t len,
                       size_t *retlen, const uint8_t *buf)
{
    struct _nand_adapter *a = mtd->priv;
    uint32_t ps = a->dev->page_size;
    size_t total = 0;

    while (len >= ps)
    {
        uint32_t page = _page(a, to);
        int ret;

        ret = a->ops->write_page(a->dev, page, buf, ps, RT_NULL, 0);
        if (ret != RT_MTD_EOK)
        {
            if (retlen)
                *retlen = total;
            return -RT_EIO;
        }

        total += ps;
        buf   += ps;
        to    += ps;
        len   -= ps;
    }

    if (retlen)
        *retlen = total;
    return 0;
}

static int _nand_erase(struct rt_mtd_info *mtd, loff_t addr, size_t len)
{
    struct _nand_adapter *a = mtd->priv;
    uint32_t s = _block(a, addr);
    uint32_t e = _block(a, addr + len - 1);
    uint32_t i;

    for (i = s; i <= e; i++)
    {
        if (a->ops->erase_block(a->dev, i) != RT_MTD_EOK)
            return -RT_EIO;
    }
    return 0;
}

/*
 * read_oob: read full page + full OOB for ECC, then copy requested
 * portions to caller (same principle as _nand_read).
 */
static int _nand_read_oob(struct rt_mtd_info *mtd, loff_t from,
                          struct rt_mtd_oob_ops *ops)
{
    struct _nand_adapter *a = mtd->priv;
    uint32_t ps = a->dev->page_size;
    uint32_t os = a->dev->oob_size;
    rt_err_t r;

    r = a->ops->read_page(a->dev, _page(a, from),
                          a->page_buf, ps,
                          a->oob_buf, os);
    if (r != RT_MTD_EOK && r != RT_MTD_EECC_CORRECT)
        return -RT_EIO;

    if (ops->datbuf && ops->len)
        rt_memcpy(ops->datbuf, a->page_buf, ops->len < ps ? ops->len : ps);
    if (ops->oobbuf && ops->ooblen)
        rt_memcpy(ops->oobbuf, a->oob_buf, ops->ooblen < os ? ops->ooblen : os);
    return 0;
}

/*
 * write_oob: write full page + full OOB. Unlike _nand_write, the OOB
 * path must write both data and OOB together (single page program).
 */
static int _nand_write_oob(struct rt_mtd_info *mtd, loff_t to,
                           struct rt_mtd_oob_ops *ops)
{
    struct _nand_adapter *a = mtd->priv;
    uint32_t ps = a->dev->page_size;
    uint32_t os = a->dev->oob_size;
    rt_err_t r;

    /* merge caller's data/OOB into full-page buffers */
    rt_memcpy(a->page_buf, ops->datbuf, ops->len < ps ? ops->len : ps);
    rt_memcpy(a->oob_buf, ops->oobbuf, ops->ooblen < os ? ops->ooblen : os);

    r = a->ops->write_page(a->dev, _page(a, to),
                           a->page_buf, ps,
                           a->oob_buf, os);
    if (r != RT_MTD_EOK)
        return -RT_EIO;
    return 0;
}

static int _nand_block_isbad(struct rt_mtd_info *mtd, loff_t ofs)
{
    struct _nand_adapter *a = mtd->priv;

    if (!a->ops->check_block)
        return 0;
    return a->ops->check_block(a->dev, _block(a, ofs)) != RT_MTD_EOK;
}

static int _nand_block_markbad(struct rt_mtd_info *mtd, loff_t ofs)
{
    struct _nand_adapter *a = mtd->priv;

    if (!a->ops->mark_badblock)
        return -RT_ENOSYS;
    return a->ops->mark_badblock(a->dev, _block(a, ofs)) == RT_MTD_EOK ? 0 : -RT_EIO;
}

static const struct rt_mtd_ops _ops =
{
    .read          = _nand_read,
    .write         = _nand_write,
    .erase         = _nand_erase,
    .read_oob      = _nand_read_oob,
    .write_oob     = _nand_write_oob,
    .block_isbad   = _nand_block_isbad,
    .block_markbad = _nand_block_markbad,
};

int rt_mtd_nand_adapter_init(struct rt_mtd_info *mtd,
                             struct rt_mtd_nand_device *nd,
                             const struct rt_mtd_nand_driver_ops *ops)
{
    struct _nand_adapter *a;

    RT_ASSERT(mtd && nd && ops && ops->read_page && ops->write_page && ops->erase_block);

    a = rt_malloc(sizeof(*a));
    if (!a)
        return -RT_ENOMEM;

    a->dev        = nd;
    a->ops        = ops;
    a->block_size = nd->pages_per_block * nd->page_size;

    a->page_buf = (uint8_t *)rt_malloc(nd->page_size);
    a->oob_buf  = (uint8_t *)rt_malloc(nd->oob_size);
    if (!a->page_buf || !a->oob_buf)
    {
        rt_free(a->page_buf);
        rt_free(a->oob_buf);
        rt_free(a);
        return -RT_ENOMEM;
    }

    rt_memset(mtd, 0, sizeof(*mtd));
    mtd->type      = RT_MTD_NANDFLASH;
    mtd->flags     = RT_MTD_WRITEABLE;
    mtd->erasesize = a->block_size;
    mtd->writesize = nd->page_size;
    mtd->oobsize   = nd->oob_size;
    mtd->size      = (uint64_t)nd->block_total * a->block_size;
    mtd->ops       = &_ops;
    mtd->priv      = a;

    LOG_I("NAND: size=%lu es=%lu ws=%lu oob=%lu blocks=%lu",
          (unsigned long)mtd->size, (unsigned long)mtd->erasesize,
          (unsigned long)mtd->writesize, (unsigned long)mtd->oobsize,
          (unsigned long)nd->block_total);
    return RT_EOK;
}
