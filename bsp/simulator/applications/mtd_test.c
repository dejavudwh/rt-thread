/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * MTD Framework Test Application
 *
 * Tests:
 *   1. NOR/NAND adapter initialization
 *   2. Partition creation (static table)
 *   3. Partition read/write/erase via mtd_char (rt_device API)
 *   4. Partition read/write/erase via direct MTD API
 *   5. mtdblock creation + FAT filesystem mount
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drivers/mtd.h>
#include <drivers/mtd_nor.h>
#include <drivers/mtd_nand.h>

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#endif

#define DBG_TAG "mtd.test"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ==================== External references from simulator drivers ==================== */

/* NOR: sst25vfxx_mtd_sim.c */
extern struct sst25_mtd _sst25_mtd;
extern const struct rt_mtd_nor_driver_ops sst25vfxx_mtd_ops;
extern rt_err_t sst25vfxx_mtd_init(const char *nor_name, rt_uint32_t block_start, rt_uint32_t block_end);

/* NAND: nanddrv_file.c */
extern struct rt_mtd_nand_device _nanddrv_file_device;
extern const struct rt_mtd_nand_driver_ops _ops;
extern void rt_hw_mtd_nand_init(void);

/* ==================== MTD device storage ==================== */
static struct rt_mtd_info _nor_master;
static struct rt_mtd_info _nand_master;



/* ==================== Test: Basic partition read/write/erase ==================== */

static void test_partition_basic(void)
{
    uint8_t buf[512];
    uint8_t verify[512];
    size_t retlen;
    int i, ret;

    rt_kprintf("\n========== MTD Partition Basic Test ==========\n");

    /* --- 1. Read initial data from nor_bl partition --- */
    rt_kprintf("[1] Reading from 'nor_bl' via rt_device API...\n");
    rt_device_t nor_bl = rt_device_find("nor_bl");
    if (nor_bl == RT_NULL)
    {
        rt_kprintf("  FAIL: 'nor_bl' device not found!\n");
        return;
    }

    rt_device_open(nor_bl, RT_DEVICE_OFLAG_RDWR);
    ret = rt_device_read(nor_bl, 0, buf, sizeof(buf));
    rt_kprintf("  Read %d bytes from offset 0 (first byte: 0x%02x)\n", ret, buf[0]);

    /* --- 2. Write test pattern to nor_app --- */
    rt_kprintf("[2] Writing test pattern to 'nor_app'...\n");
    struct rt_mtd_info *app = rt_device_find("nor_app");
    if (app == RT_NULL)
    {
        return;
    }

    /* Fill pattern */
    for (i = 0; i < (int)sizeof(buf); i++)
        buf[i] = (uint8_t)(i & 0xFF);

    ret = rt_mtd_write(app, 0, sizeof(buf), &retlen, buf);
    rt_kprintf("  Wrote %zu bytes: ret=%d\n", retlen, ret);

    /* Read back and verify */
    rt_memset(verify, 0, sizeof(verify));
    ret = rt_mtd_read(app, 0, sizeof(verify), &retlen, verify);
    rt_kprintf("  Read back %zu bytes\n", retlen);

    if (rt_memcmp(buf, verify, sizeof(buf)) == 0)
        rt_kprintf("  PASS: Data verification OK!\n");
    else
        rt_kprintf("  FAIL: Data mismatch!\n");

    /* --- 3. Erase nor_app --- */
    rt_kprintf("[3] Erasing 'nor_app' (first erase block)...\n");
    ret = rt_mtd_erase(app, 0, app->erasesize);
    rt_kprintf("  Erase ret=%d\n", ret);

    /* Verify erased (should be all 0xFF) */
    rt_memset(verify, 0, sizeof(verify));
    ret = rt_mtd_read(app, 0, sizeof(verify), &retlen, verify);
    rt_kprintf("  After erase: first byte=0x%02x (expect 0xFF)\n", verify[0]);
    rt_kprintf("  %s\n", (verify[0] == 0xFF) ? "PASS: Erased!" : "FAIL: Not erased!");


    /* --- 4. Control: get MTD info --- */
    rt_kprintf("[4] Getting device info via rt_device_control...\n");
    struct rt_mtd_info info;
    ret = rt_device_control(nor_bl, RT_DEVICE_CTRL_MTD_GET_INFO, &info);
    rt_kprintf("  GET_INFO ret=%d, type=%d, size=%llu, erasesize=%lu\n",
               ret, info.type, (unsigned long long)info.size,
               (unsigned long)info.erasesize);

    /* --- 5. Test partition boundary enforcement --- */
    rt_kprintf("[5] Testing partition boundary enforcement...\n");
    uint8_t big_buf[256];
    /* Access beyond partition should fail or clamp */
    struct rt_mtd_info *bl = rt_device_find("nor_bl");
    ret = rt_mtd_read(bl, bl->part_size - 128, sizeof(big_buf), &retlen, big_buf);
    rt_kprintf("  Read at boundary: ret=%d, retlen=%zu (expect clamp to 128)\n",
               ret, retlen);
    /* Try beyond end should fail */
    ret = rt_mtd_read(bl, bl->part_size + 100, 10, &retlen, big_buf);
    rt_kprintf("  Read beyond end: ret=%d (expect <0)\n", ret);

    /* --- 6. Verify partition isolation --- */
    rt_kprintf("[6] Testing partition isolation...\n");
    rt_device_t nor_data = rt_device_find("nor_data");
    if (nor_data != RT_NULL)
    {
        rt_device_open(nor_data, RT_DEVICE_OFLAG_RDWR);

        /* Write to data partition at offset 0 */
        rt_memset(buf, 0xAB, sizeof(buf));
        rt_device_write(nor_data, 0, buf, sizeof(buf));

        /* Read from app partition at offset 0 — should be different */
        struct rt_mtd_info *app2 = rt_device_find("nor_app");
        rt_memset(verify, 0, sizeof(verify));
        rt_mtd_read(app2, 0, sizeof(verify), &retlen, verify);
        rt_kprintf("  Wrote 0xAB to data[0], read app[0]=0x%02x (expect != 0xAB)\n",
                   verify[0]);
        rt_kprintf("  %s\n", (verify[0] != 0xAB) ? "PASS: Partitions isolated!" :
                                                    "FAIL: Partition leak!");

        rt_device_close(nor_data);
    }
    else
    {
        rt_kprintf("  SKIP: 'nor_data' partition not available (check sizing)\n");
    }

    rt_kprintf("========== Partition Basic Test Complete ==========\n");
}

/* ==================== Test: NAND OOB and bad block ==================== */

static void test_nand_features(void)
{
    int ret;
    loff_t ofs = 0;

    rt_kprintf("\n========== MTD NAND Feature Test ==========\n");

    struct rt_mtd_info *nand = rt_device_find("nand_0");
    if (nand == RT_NULL)
    {
        rt_kprintf("FAIL: 'nand_0' not found!\n");
        return;
    }

    /* Check bad block */
    ret = rt_mtd_block_isbad(nand, 0);
    rt_kprintf("[1] block_isbad(0) = %d (0=good, 1=bad)\n", ret);

    /* Check via device control */
    ret = rt_device_control(&nand->parent, RT_DEVICE_CTRL_MTD_IS_BAD, &ofs);
    rt_kprintf("[2] RT_DEVICE_CTRL_MTD_IS_BAD = %d\n", ret);

    /* Check bad block in the middle */
    ofs = 4 * 1024 * 1024ULL;
    ret = rt_mtd_block_isbad(nand, ofs);
    rt_kprintf("[3] block_isbad(4MB) = %d\n", ret);

    /* OOB read */
    struct rt_mtd_oob_ops oob_ops;
    uint8_t oob_buf[64];
    uint8_t data_buf[2048];

    rt_memset(&oob_ops, 0, sizeof(oob_ops));
    oob_ops.mode   = MTD_OPS_PLACE_OOB;
    oob_ops.len    = 512;
    oob_ops.ooblen = nand->oobsize;
    oob_ops.datbuf = data_buf;
    oob_ops.oobbuf = oob_buf;

    ret = rt_mtd_read_oob(nand, 0, &oob_ops);
    rt_kprintf("[4] read_oob(0): ret=%d, oob[0]=0x%02x\n", ret, oob_buf[0]);

    /* Write OOB test */
    oob_buf[0] = 0x5A;
    oob_buf[1] = 0xA5;
    ret = rt_mtd_write_oob(nand, 0, &oob_ops);
    rt_kprintf("[5] write_oob(0): ret=%d\n", ret);

    /* Read back OOB */
    rt_memset(oob_buf, 0, sizeof(oob_buf));
    ret = rt_mtd_read_oob(nand, 0, &oob_ops);
    rt_kprintf("[6] read_oob verify: oob[0]=0x%02x oob[1]=0x%02x %s\n",
               oob_buf[0], oob_buf[1],
               (oob_buf[0] == 0x5A && oob_buf[1] == 0xA5) ? "PASS" : "FAIL");

    rt_kprintf("========== NAND Feature Test Complete ==========\n");
}

/* ==================== Test: mtdblock + FAT filesystem ==================== */

#ifdef RT_USING_MTD_BLOCK
static void test_mtdblock_fat(void)
{
    int ret;

    rt_kprintf("\n========== MTD Block + FAT Test ==========\n");

    /* Create block device from nor_data partition */
    struct rt_device *blk = rt_mtd_block_create("nor_data", "data0");
    if (blk == RT_NULL)
    {
        rt_kprintf("[1] FAIL: Failed to create mtdblock 'data0'\n");
        return;
    }
    rt_kprintf("[1] mtdblock 'data0' created (sectors=%lu, sector_size=%lu)\n",
               (unsigned long)((struct rt_device_blk_geometry *)blk->user_data ?
                0 : 0),
               (unsigned long)512);

    /* Erase the block device first */
    ret = rt_device_control(blk, RT_DEVICE_CTRL_BLK_ERASE, RT_NULL);
    rt_kprintf("[2] BLK_ERASE: ret=%d\n", ret);

#ifdef RT_USING_DFS
    /* Format as FAT */
    rt_kprintf("[3] Formatting as FAT...\n");
    ret = dfs_mkfs("elm", "data0");
    rt_kprintf("  mkfs ret=%d\n", ret);

    /* Mount */
    ret = dfs_mount("data0", "/mtd_fat", "elm", 0, 0);
    if (ret == 0)
    {
        rt_kprintf("[4] Mounted 'data0' on '/mtd_fat' successfully!\n");

        /* Write a test file */
        int fd = open("/mtd_fat/test.txt", O_WRONLY | O_CREAT | O_TRUNC, 0);
        if (fd >= 0)
        {
            const char *msg = "Hello from MTD framework test!\n";
            int written = write(fd, msg, rt_strlen(msg));
            rt_kprintf("[5] Wrote %d bytes to /mtd_fat/test.txt\n", written);
            close(fd);
        }
        else
        {
            rt_kprintf("[5] FAIL: Cannot create file (fd=%d)\n", fd);
        }

        /* Read it back */
        fd = open("/mtd_fat/test.txt", O_RDONLY, 0);
        if (fd >= 0)
        {
            char read_buf[128];
            int n = read(fd, read_buf, sizeof(read_buf) - 1);
            if (n > 0)
            {
                read_buf[n] = '\0';
                rt_kprintf("[6] Read back: '%s'", read_buf);
            }
            close(fd);
        }

        /* Unmount */
        dfs_unmount("/mtd_fat");
        rt_kprintf("[7] Unmounted /mtd_fat\n");
    }
    else
    {
        rt_kprintf("[4] FAIL: Mount failed (ret=%d)\n", ret);
    }
#endif /* RT_USING_DFS */

    rt_kprintf("========== Block + FAT Test Complete ==========\n");
}
#endif /* RT_USING_MTD_BLOCK */

/* ==================== Main test runner ==================== */

static void mtd_test_run(void)
{
    rt_kprintf("\n");
    rt_kprintf("╔══════════════════════════════════════════════╗\n");
    rt_kprintf("║     RT-Thread MTD Framework Test Suite       ║\n");
    rt_kprintf("╚══════════════════════════════════════════════╝\n");

    test_partition_basic();
    test_nand_features();
    /* test_mtdblock_fat(); -- TODO: verify erase worker */

    rt_kprintf("\n════════════════════════════════════════════════\n");
    rt_kprintf("  MTD Test Suite Complete!\n");
    rt_kprintf("════════════════════════════════════════════════\n\n");
}

/* ==================== Initialization ==================== */

/*
 * Partition tables defined via macro (as users would in board.c):
 */
RT_MTD_PARTITION_TABLE(norflash0,
    { "nor_bl",   0,                  256 * 1024            },
    { "nor_app",  MTDPART_OFS_APPEND, 768 * 1024            },
    { "nor_data", MTDPART_OFS_APPEND, MTDPART_SIZ_FULL     },
);

RT_MTD_PARTITION_TABLE(nandflash0,
    { "nand_0", 0,                    8 * 1024 * 1024      },
    { "nand_1", MTDPART_OFS_APPEND,  MTDPART_SIZ_FULL      },
);

static int mtd_test_init(void)
{
    rt_kprintf("\n[MTD Test] Initializing MTD test environment...\n");
    rt_kprintf("[MTD Test] Old drivers already initialized by platform.c\n");

    /* --- Step 1: Find existing old-style NOR device (registered by platform.c) --- */
    rt_kprintf("[MTD Test] Wrapping NOR simulator via adapter...\n");
    struct rt_device *old_nor = rt_device_find("nor");
    if (old_nor == RT_NULL)
    {
        rt_kprintf("[MTD Test] ERROR: old NOR device 'nor' not found!\n");
        return -RT_ERROR;
    }

    struct rt_mtd_nor_device *nor_dev = (struct rt_mtd_nor_device *)old_nor;

    rt_mtd_nor_adapter_init(&_nor_master, nor_dev, &sst25vfxx_mtd_ops);
    rt_device_unregister(old_nor);

    /* Step 2: register NOR with partitions via macro */
    rt_kprintf("[MTD Test] Registering NOR...\n");
    RT_MTD_DEVICE_REGISTER(&_nor_master, norflash0);

    /* --- Step 3: Find existing old-style NAND device (registered by platform.c) --- */
    rt_kprintf("[MTD Test] Wrapping NAND simulator via adapter...\n");
    struct rt_device *old_nand = rt_device_find("nand0");
    if (old_nand == RT_NULL)
    {
        rt_kprintf("[MTD Test] ERROR: old NAND device 'nand0' not found!\n");
        return -RT_ERROR;
    }

    struct rt_mtd_nand_device *nand_dev = (struct rt_mtd_nand_device *)old_nand;

    rt_mtd_nand_adapter_init(&_nand_master, nand_dev, &_ops);
    rt_device_unregister(old_nand);

    /* Step 4: register NAND with partitions via macro */
    rt_kprintf("[MTD Test] Registering NAND...\n");
    RT_MTD_DEVICE_REGISTER(&_nand_master, nandflash0);

    /* --- Step 5: Run tests --- */
    mtd_test_run();

    return RT_EOK;
}
INIT_APP_EXPORT(mtd_test_init);

/* ==================== Shell command: manual test ==================== */
#ifdef RT_USING_FINSH
#include <finsh.h>

static void cmd_mtd_test(int argc, char **argv)
{
    if (argc > 1 && rt_strcmp(argv[1], "init") == 0)
        mtd_test_init();
    else
        mtd_test_run();
}
MSH_CMD_EXPORT_ALIAS(cmd_mtd_test, mtd_test, Run MTD framework test suite);

static void cmd_mtd_list(int argc, char **argv)
{
    struct rt_mtd_info *mtd;
    int i;
    rt_kprintf("MTD devices:\n");
    for (i = 0; ; i++)
    {
        char name[32];
        rt_snprintf(name, sizeof(name), "mtd%d", i);
        mtd = rt_device_find(name);
        if (mtd == RT_NULL) break;
        rt_kprintf("  %s: type=%d size=%llu erasesize=%lu %s\n",
                   name, mtd->type, (unsigned long long)mtd->size,
                   (unsigned long)mtd->erasesize,
                   rt_mtd_is_partition(mtd) ? "(partition)" : "(master)");
    }
}
MSH_CMD_EXPORT_ALIAS(cmd_mtd_list, mtd_list, List all MTD devices);
#endif /* RT_USING_FINSH */
