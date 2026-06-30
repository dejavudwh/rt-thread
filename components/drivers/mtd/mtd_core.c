/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * MTD core: global device table, partition chain resolution,
 * bounds/alignment checks, dispatch to master ops.
 *
 * Only mtd_char / mtd_block are registered as rt_device.
 * mtd_core maintains its own _mtd_table[] for all mtd_info
 * instances (masters and partitions).
 *
 * When an mtd_info is a partition, all API calls resolve the
 * master chain (accumulating part_offset) before dispatching
 * to the master's ops. Partitions do NOT have their own ops.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd.h>

#define DBG_TAG "mtd.core"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifndef RT_MTD_MAX_DEVICES
#define RT_MTD_MAX_DEVICES  16
#endif

/* ---- global table managed by mtd_core ---- */
static struct rt_mtd_info *_mtd_table[RT_MTD_MAX_DEVICES];
static int _mtd_count;
static struct rt_spinlock _mtd_lock;

/* Called by mtd_char when an mtd_info enters / leaves the device tree. */
void _mtd_core_table_add(struct rt_mtd_info *mtd)
{
    rt_spin_lock(&_mtd_lock);
    if (_mtd_count < RT_MTD_MAX_DEVICES)
        _mtd_table[_mtd_count++] = mtd;
    rt_spin_unlock(&_mtd_lock);
}

void _mtd_core_table_remove(struct rt_mtd_info *mtd)
{
    int i;

    rt_spin_lock(&_mtd_lock);
    for (i = 0; i < _mtd_count; i++)
    {
        if (_mtd_table[i] == mtd)
        {
            _mtd_count--;
            if (i < _mtd_count)
                rt_memmove(&_mtd_table[i], &_mtd_table[i + 1],
                          (_mtd_count - i) * sizeof(mtd));
            _mtd_table[_mtd_count] = RT_NULL;
            break;
        }
    }
    rt_spin_unlock(&_mtd_lock);
}

/*
 * Resolve partition chain. If mtd is a partition, walk up to the
 * master, accumulating part_offset along the way. On return *pmtd
 * is the master device and *off has the cumulative offset added.
 */
static void _resolve(struct rt_mtd_info **pmtd, loff_t *off)
{
    while ((*pmtd)->master)
    {
        *off += (loff_t)(*pmtd)->part_offset;
        *pmtd = (*pmtd)->master;
    }
}

static int _bounds_ok(struct rt_mtd_info *mtd, loff_t ofs, size_t len)
{
    return ((uint64_t)ofs < mtd->size && (uint64_t)(ofs + len) <= mtd->size);
}

/* ==================== public API ==================== */

int rt_mtd_read(struct rt_mtd_info *mtd, loff_t from, size_t len,
                size_t *retlen, uint8_t *buf)
{
    RT_ASSERT(mtd && buf);

    if (!_bounds_ok(mtd, from, len))
    {
        if (retlen) *retlen = 0;
        return -RT_EINVAL;
    }

    /* resolve partition → master, adjusting offset */
    _resolve(&mtd, &from);
    return mtd->ops->read(mtd, from, len, retlen, buf);
}

int rt_mtd_write(struct rt_mtd_info *mtd, loff_t to, size_t len,
                 size_t *retlen, const uint8_t *buf)
{
    RT_ASSERT(mtd && buf);

    if (!(mtd->flags & RT_MTD_WRITEABLE))
        return -RT_ERROR;

    if (!_bounds_ok(mtd, to, len))
    {
        if (retlen) *retlen = 0;
        return -RT_EINVAL;
    }

    /* writesize alignment — enforced on the resolved master offset */
    if (mtd->writesize)
    {
        if ((uint64_t)to & (mtd->writesize - 1))
        {
            if (retlen) *retlen = 0;
            return -RT_EINVAL;
        }
        if (len & (mtd->writesize - 1))
        {
            if (retlen) *retlen = 0;
            return -RT_EINVAL;
        }
    }

    _resolve(&mtd, &to);
    return mtd->ops->write(mtd, to, len, retlen, buf);
}

int rt_mtd_erase(struct rt_mtd_info *mtd, loff_t addr, size_t len)
{
    RT_ASSERT(mtd);

    if (!_bounds_ok(mtd, addr, len))
        return -RT_EINVAL;
    if (!mtd->erasesize)
        return -RT_EINVAL;
    if ((uint64_t)addr & (mtd->erasesize - 1))
        return -RT_EINVAL;
    if (len & (mtd->erasesize - 1))
        return -RT_EINVAL;

    _resolve(&mtd, &addr);
    return mtd->ops->erase(mtd, addr, len);
}

int rt_mtd_read_oob(struct rt_mtd_info *mtd, loff_t from,
                    struct rt_mtd_oob_ops *ops)
{
    RT_ASSERT(mtd);
    _resolve(&mtd, &from);
    if (!mtd->ops->read_oob) return -RT_ENOSYS;
    return mtd->ops->read_oob(mtd, from, ops);
}

int rt_mtd_write_oob(struct rt_mtd_info *mtd, loff_t to,
                     struct rt_mtd_oob_ops *ops)
{
    RT_ASSERT(mtd);
    _resolve(&mtd, &to);
    if (!mtd->ops->write_oob) return -RT_ENOSYS;
    return mtd->ops->write_oob(mtd, to, ops);
}

int rt_mtd_block_isbad(struct rt_mtd_info *mtd, loff_t ofs)
{
    RT_ASSERT(mtd);
    _resolve(&mtd, &ofs);
    if (!mtd->ops->block_isbad) return 0;
    return mtd->ops->block_isbad(mtd, ofs);
}

int rt_mtd_block_markbad(struct rt_mtd_info *mtd, loff_t ofs)
{
    RT_ASSERT(mtd);
    _resolve(&mtd, &ofs);
    if (!mtd->ops->block_markbad) return -RT_ENOSYS;
    return mtd->ops->block_markbad(mtd, ofs);
}

int rt_mtd_is_partition(struct rt_mtd_info *mtd)
{
    return mtd && mtd->master ? 1 : 0;
}

struct rt_mtd_info *rt_mtd_get_master(struct rt_mtd_info *mtd)
{
    if (!mtd) return RT_NULL;
    while (mtd->master) mtd = mtd->master;
    return mtd;
}

/*
 * One-shot: register master + create partitions (≡ Linux
 * mtd_device_parse_register). Use RT_MTD_DEVICE_REGISTER(mtd, name)
 * macro with a partition table defined by RT_MTD_PARTITION_TABLE(name, ...)
 * in board.c, or pass the table directly.
 */
int rt_mtd_device_register(struct rt_mtd_info *mtd, const char *name,
                           const struct rt_mtd_partition *parts,
                           int nr_parts)
{
    int ret;

    ret = rt_mtd_char_register(mtd, name);
    if (ret)
        return ret;

    if (parts && nr_parts > 0)
        rt_mtd_partition_register(mtd, parts, nr_parts);

    return RT_EOK;
}

/*
 * Unregister a master and its partitions. Finds all partitions
 * whose ->master points to this mtd in the global table, unregisters
 * and frees them first, then unregisters the master itself.
 *
 * NOTE: does NOT free the master mtd_info — it may be static.
 */
int rt_mtd_device_unregister(struct rt_mtd_info *mtd)
{
    int i;

    RT_ASSERT(mtd && !mtd->master); /* must be a master */

    /* unregister and free all partitions first */
    rt_spin_lock(&_mtd_lock);
    for (i = _mtd_count - 1; i >= 0; i--)
    {
        struct rt_mtd_info *child = _mtd_table[i];
        if (child->master == mtd)
        {
            rt_spin_unlock(&_mtd_lock);
            rt_mtd_char_unregister(child);
            rt_free(child);
            rt_spin_lock(&_mtd_lock);
        }
    }
    rt_spin_unlock(&_mtd_lock);

    return rt_mtd_char_unregister(mtd);
}

int rt_mtd_init(void)
{
    static int done;
    if (done) return RT_EOK;
    done = 1;

    _mtd_count = 0;
    rt_memset(_mtd_table, 0, sizeof(_mtd_table));
    rt_spin_lock_init(&_mtd_lock);

    LOG_I("MTD framework initialized");
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(rt_mtd_init);
