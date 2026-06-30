/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * mtd_partition: create partition entries from a static table.
 *
 * A partition is an mtd_info with ops == NULL, a master pointer,
 * and a part_offset. All I/O goes through mtd_core which resolves
 * the master chain by recursively accumulating part_offset, then
 * dispatches to the master's ops.
 *
 * This follows the Linux approach (struct mtd_info.part + recursive
 * offset resolution), without duplicating ops tables per partition.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd.h>

#define DBG_TAG "mtd.part"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

int rt_mtd_partition_register(struct rt_mtd_info *master,
                              const struct rt_mtd_partition *parts,
                              int nr_parts)
{
    uint64_t cur = 0;
    int i, n = 0;

    RT_ASSERT(master && parts && nr_parts > 0);
    if (master->master)
    {
        LOG_E("cannot nest partitions");
        return -RT_EINVAL;
    }

    LOG_I("creating %d partitions on '%s'",
          nr_parts, master->parent.parent.name);

    for (i = 0; i < nr_parts; i++, n++)
    {
        const struct rt_mtd_partition *p = &parts[i];
        struct rt_mtd_info *child;
        uint64_t off, sz;

        off = (p->offset == MTDPART_OFS_APPEND) ? cur : p->offset;
        sz  = (p->size == MTDPART_SIZ_FULL) ? (master->size - off) : p->size;

        if (off >= master->size)
        {
            LOG_E("'%s' offset exceeds master", p->name);
            break;
        }
        if (off + sz > master->size)
        {
            LOG_W("'%s' truncated", p->name);
            sz = master->size - off;
        }

        child = (struct rt_mtd_info *)rt_malloc(sizeof(*child));
        if (!child) break;
        rt_memset(child, 0, sizeof(*child));

        /*
         * Partition inherits geometry from master but has NO ops.
         * mtd_core resolves the offset chain at access time.
         */
        child->type        = master->type;
        child->flags       = master->flags & ~p->mask_flags;
        child->size        = sz;
        child->erasesize   = master->erasesize;
        child->writesize   = master->writesize;
        child->oobsize     = master->oobsize;
        child->ops         = RT_NULL;   /* resolved through master */
        child->master      = master;
        child->part_offset = off;
        child->part_size   = sz;

        if (rt_mtd_char_register(child, p->name))
        {
            LOG_E("register '%s' failed", p->name);
            rt_free(child);
            break;
        }

        LOG_I("  [%d] '%s': off=0x%lx size=0x%lx (%lu KB)",
              i, p->name, (unsigned long)off, (unsigned long)sz,
              (unsigned long)(sz / 1024));
        cur = off + sz;
    }

    LOG_I("registered %d partitions on '%s'", n, master->parent.parent.name);
    return n > 0 ? RT_EOK : -RT_ERROR;
}
