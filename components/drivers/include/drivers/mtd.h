/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Unified MTD framework — follows Linux MTD design.
 */

#ifndef __MTD_FRAMEWORK_H__
#define __MTD_FRAMEWORK_H__

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- device types ---- */
#define RT_MTD_NORFLASH     1
#define RT_MTD_NANDFLASH    2
#define RT_MTD_SPI_NOR      3
#define RT_MTD_DATAFLASH    4
#define RT_MTD_RAM          5
#define RT_MTD_ROM          6

/* ---- device flags ---- */
#define RT_MTD_WRITEABLE    0x0001

/* ---- OOB modes ---- */
#define MTD_OPS_PLACE_OOB   0
#define MTD_OPS_AUTO_OOB    1
#define MTD_OPS_RAW         2

/* ---- partition special values ---- */
#define MTDPART_OFS_APPEND  ((uint64_t)-1)
#define MTDPART_SIZ_FULL    0

/* ---- control commands ---- */
/* +1: RT_DEVICE_CTRL_MTD_FORMAT (defined in <drivers/classes/mtd.h>) */
#define RT_DEVICE_CTRL_MTD_GET_INFO     (RT_DEVICE_CTRL_BASE(MTD) + 2)
#define RT_DEVICE_CTRL_MTD_ERASE        (RT_DEVICE_CTRL_BASE(MTD) + 3)
#define RT_DEVICE_CTRL_MTD_IS_BAD       (RT_DEVICE_CTRL_BASE(MTD) + 4)
#define RT_DEVICE_CTRL_MTD_MARK_BAD     (RT_DEVICE_CTRL_BASE(MTD) + 5)

/*
 * User partition table macro. Put this in board.c:
 *
 *   RT_MTD_PARTITION_TABLE(norflash0,
 *       { "bl",   0,                  256 * 1024            },
 *       { "app",  MTDPART_OFS_APPEND, 2 * 1024 * 1024      },
 *       { "data", MTDPART_OFS_APPEND, MTDPART_SIZ_FULL     },
 *   );
 *
 * Then register with:
 *   RT_MTD_DEVICE_REGISTER(&mtd, norflash0);
 */
#define RT_MTD_PARTITION_TABLE(name, ...) \
    static const struct rt_mtd_partition _mtd_parts_##name[] = { __VA_ARGS__ }

#define RT_MTD_DEVICE_REGISTER(mtd, name) \
    rt_mtd_device_register(mtd, #name, _mtd_parts_##name, \
        sizeof(_mtd_parts_##name) / sizeof(_mtd_parts_##name[0]))

/* ---- forward declarations ---- */
struct rt_mtd_info;
struct rt_mtd_nor_driver_ops;
struct rt_mtd_nand_driver_ops;
struct rt_mtd_nor_device;
struct rt_mtd_nand_device;

/* ---- OOB operation descriptor ---- */
struct rt_mtd_oob_ops
{
    uint32_t mode;
    size_t   len;
    size_t   ooblen;
    size_t   ooboffs;
    uint8_t *datbuf;
    uint8_t *oobbuf;
};

/*
 * Unified ops table. Only master devices have ops set; partitions
 * resolve to their master through the offset chain in mtd_core.
 */
struct rt_mtd_ops
{
    int (*read)(struct rt_mtd_info *mtd, loff_t from, size_t len,
                size_t *retlen, uint8_t *buf);
    int (*write)(struct rt_mtd_info *mtd, loff_t to, size_t len,
                 size_t *retlen, const uint8_t *buf);
    int (*erase)(struct rt_mtd_info *mtd, loff_t addr, size_t len);

    int (*read_oob)(struct rt_mtd_info *mtd, loff_t from,
                    struct rt_mtd_oob_ops *ops);
    int (*write_oob)(struct rt_mtd_info *mtd, loff_t to,
                     struct rt_mtd_oob_ops *ops);
    int (*block_isbad)(struct rt_mtd_info *mtd, loff_t ofs);
    int (*block_markbad)(struct rt_mtd_info *mtd, loff_t ofs);
};

/*
 * Unified MTD device descriptor.
 *
 * Masters have ops != NULL and master == NULL.
 * Partitions have ops == NULL, master pointing to their parent.
 * mtd_core resolves the master chain and adjusts offsets before
 * dispatching to the master's ops.
 */
struct rt_mtd_info
{
    struct rt_device parent;        /* RT-Thread device (registered by mtd_char) */

    uint8_t  type;
    uint32_t flags;
    uint64_t size;
    uint32_t erasesize;
    uint32_t writesize;
    uint32_t oobsize;

    const struct rt_mtd_ops *ops;   /* NULL for partitions */

    /* partition linkage — resolved by mtd_core */
    struct rt_mtd_info *master;
    uint64_t part_offset;
    uint64_t part_size;

    void *priv;
};

/* ---- static partition descriptor ---- */
struct rt_mtd_partition
{
    const char *name;
    uint64_t    offset;
    uint64_t    size;
    uint32_t    mask_flags;
};

/* ---- public API (mtd_core) ---- */

int rt_mtd_read(struct rt_mtd_info *mtd, loff_t from, size_t len,
                size_t *retlen, uint8_t *buf);
int rt_mtd_write(struct rt_mtd_info *mtd, loff_t to, size_t len,
                 size_t *retlen, const uint8_t *buf);
int rt_mtd_erase(struct rt_mtd_info *mtd, loff_t addr, size_t len);
int rt_mtd_read_oob(struct rt_mtd_info *mtd, loff_t from,
                    struct rt_mtd_oob_ops *ops);
int rt_mtd_write_oob(struct rt_mtd_info *mtd, loff_t to,
                     struct rt_mtd_oob_ops *ops);
int rt_mtd_block_isbad(struct rt_mtd_info *mtd, loff_t ofs);
int rt_mtd_block_markbad(struct rt_mtd_info *mtd, loff_t ofs);
int rt_mtd_is_partition(struct rt_mtd_info *mtd);
struct rt_mtd_info *rt_mtd_get_master(struct rt_mtd_info *mtd);

/*
 * Register / unregister an mtd_info as an RT-Thread char device.
 * Called by rt_mtd_device_register() and mtd_partition internally.
 */
int rt_mtd_char_register(struct rt_mtd_info *mtd, const char *name);
int rt_mtd_char_unregister(struct rt_mtd_info *mtd);

/*
 * One-shot: register master + create partitions.
 *
 *   // with macro:
 *   RT_MTD_PARTITION_TABLE(norflash0, { ... }, { ... });
 *   RT_MTD_DEVICE_REGISTER(&mtd, norflash0);
 *
 *   // or programmatic:
 *   rt_mtd_device_register(&mtd, "norflash0", parts, nr_parts);
 */
int rt_mtd_device_register(struct rt_mtd_info *mtd, const char *name,
                           const struct rt_mtd_partition *parts,
                           int nr_parts);
int rt_mtd_device_unregister(struct rt_mtd_info *mtd);
int rt_mtd_partition_register(struct rt_mtd_info *master,
                              const struct rt_mtd_partition *parts,
                              int nr_parts);

/* block device adapter */
#ifdef RT_USING_MTD_BLOCK
struct rt_device *rt_mtd_block_create(const char *mtd_name,
                                      const char *blk_name);
int rt_mtd_block_destroy(struct rt_device *blk_dev);
#endif

/* adapter init */
#ifdef RT_USING_MTD_NOR
int rt_mtd_nor_adapter_init(struct rt_mtd_info *mtd,
                            struct rt_mtd_nor_device *nor_dev,
                            const struct rt_mtd_nor_driver_ops *nor_ops);
#endif

#ifdef RT_USING_MTD_NAND
int rt_mtd_nand_adapter_init(struct rt_mtd_info *mtd,
                             struct rt_mtd_nand_device *nand_dev,
                             const struct rt_mtd_nand_driver_ops *nand_ops);
#endif

int rt_mtd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __MTD_FRAMEWORK_H__ */
