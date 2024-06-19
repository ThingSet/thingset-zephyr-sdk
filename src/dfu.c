/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>

#include <thingset.h>
#include <thingset/sdk.h>

LOG_MODULE_REGISTER(thingset_dfu, CONFIG_THINGSET_SDK_LOG_LEVEL);

#define TARGET_IMAGE_AREA FIXED_PARTITION_ID(slot1_partition)

static uint8_t bytes_buf[CONFIG_THINGSET_DFU_CHUNK_SIZE];
static THINGSET_DEFINE_BYTES(bytes_item, bytes_buf, 0);

static int32_t thingset_dfu_init();
static int32_t thingset_dfu_write();
static int32_t thingset_dfu_boot();
static void thingset_dfu_reboot_work_handler(struct k_work *work);

THINGSET_ADD_GROUP(TS_ID_ROOT, TS_ID_DFU, "DFU", THINGSET_NO_CALLBACK);
THINGSET_ADD_FN_INT32(TS_ID_DFU, TS_ID_DFU_INIT, "xInit", &thingset_dfu_init, THINGSET_ANY_RW);
THINGSET_ADD_FN_INT32(TS_ID_DFU, TS_ID_DFU_WRITE, "xWrite", &thingset_dfu_write, THINGSET_ANY_RW);
THINGSET_ADD_ITEM_BYTES(TS_ID_DFU_WRITE, TS_ID_DFU_DATA, "bData", &bytes_item, THINGSET_ANY_RW, 0);
THINGSET_ADD_FN_INT32(TS_ID_DFU, TS_ID_DFU_BOOT, "xBoot", &thingset_dfu_boot, THINGSET_ANY_RW);

static bool dfu_initialized = false;

static struct flash_img_context flash_img_ctx;

K_WORK_DELAYABLE_DEFINE(reboot_work, &thingset_dfu_reboot_work_handler);

static int32_t thingset_dfu_init()
{
    int err;

    LOG_INF("Initializing DFU");

    if (!IS_ENABLED(CONFIG_IMG_ERASE_PROGRESSIVELY)) {
        LOG_DBG("Erasing flash area");

        err = boot_erase_img_bank(TARGET_IMAGE_AREA);
        if (err) {
            LOG_ERR("Failed to erase image bank (err %d)", err);
            return err;
        }
    }

    err = flash_img_init_id(&flash_img_ctx, TARGET_IMAGE_AREA);
    if (err) {
        LOG_ERR("Failed to initialize flash img (err %d)", err);
        return err;
    }

    dfu_initialized = true;

    return 0;
}

static int32_t thingset_dfu_write()
{
    int err;

    if (!dfu_initialized) {
        LOG_ERR("DFU not initialized");
        return -EBUSY;
    }

    err = flash_img_buffered_write(&flash_img_ctx, bytes_item.bytes, bytes_item.num_bytes, false);
    if (err) {
        LOG_ERR("Failed to write data (err %d)", err);
        return err;
    }
    else {
        size_t total_bytes = flash_img_bytes_written(&flash_img_ctx);
        LOG_INF("Total bytes written: 0x%.6X (%d)", total_bytes, total_bytes);
    }

    return 0;
}

static int32_t thingset_dfu_boot()
{
    int err;
    uint8_t dummy = 0;

    /* make sure that flash_img buffer is flushed */
    flash_img_buffered_write(&flash_img_ctx, &dummy, 0, true);

    err = boot_request_upgrade(BOOT_UPGRADE_TEST);
    if (err) {
        LOG_ERR("Failed to finish DFU (err %d)", err);
        return err;
    }

    LOG_INF("DFU finished, scheduling reboot...");

    /* Schedule the reboot work to be executed after 1 second */
    k_work_schedule(&reboot_work, K_SECONDS(1));

    return 0;
}

static void thingset_dfu_reboot_work_handler(struct k_work *work)
{
    LOG_INF("Rebooting now...");
    sys_reboot(SYS_REBOOT_COLD);
}
