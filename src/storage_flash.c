/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

#include <stdio.h>

LOG_MODULE_REGISTER(thingset_storage_nvs, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * NVS header bytes:
 * - 0-1: Data objects version number
 *
 * Data starts from byte 2
 */
#define NVS_HEADER_SIZE 2

#define NVS_PARTITION storage_partition

#define THINGSET_DATA_ID 1

static struct k_work_delayable storage_work;

static struct nvs_fs fs;
static bool nvs_initialized = false;

static int data_storage_init()
{
    struct flash_pages_info page_info;
    int err;

    fs.flash_device = FIXED_PARTITION_DEVICE(NVS_PARTITION);
    if (!device_is_ready(fs.flash_device)) {
        LOG_ERR("Flash device not ready");
        return -ENODEV;
    }

    fs.offset = FIXED_PARTITION_OFFSET(NVS_PARTITION);
    err = flash_get_page_info_by_offs(fs.flash_device, fs.offset, &page_info);
    if (err) {
        LOG_ERR("Unable to get flash page info");
        return err;
    }

    fs.sector_size = page_info.size;
    fs.sector_count = FIXED_PARTITION_SIZE(NVS_PARTITION) / page_info.size;

    err = nvs_mount(&fs);
    if (err) {
        LOG_ERR("NVS mount failed: %d", err);
        return err;
    }

    nvs_initialized = true;

    return 0;
}

int thingset_storage_load()
{
    int err = 0;

    if (!nvs_initialized) {
        int err = data_storage_init();
        if (err != 0) {
            return err;
        }
    }

    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
    k_sem_take(&sbuf->lock, K_FOREVER);

    int num_bytes = nvs_read(&fs, THINGSET_DATA_ID, sbuf->data, sbuf->size);
    if (num_bytes < 0) {
        LOG_DBG("NVS empty (read error %d)", num_bytes);
        err = num_bytes;
        goto out;
    }

    LOG_HEXDUMP_DBG(sbuf->data, num_bytes, "data to be imported");

    uint16_t version = *((uint16_t *)&sbuf->data[0]);
    if (version == CONFIG_THINGSET_STORAGE_DATA_VERSION) {
        int status =
            thingset_import_data(&ts, sbuf->data + NVS_HEADER_SIZE, num_bytes - NVS_HEADER_SIZE,
                                 THINGSET_WRITE_MASK, THINGSET_BIN_IDS_VALUES);
        if (status == 0) {
            LOG_DBG("NVS read and data successfully updated");
        }
        else {
            LOG_ERR("Importing data failed with ThingSet response code 0x%X", -status);
            err = -EINVAL;
        }
    }
    else {
        LOG_WRN("NVS data ignored due to version mismatch: %d", version);
        err = -EINVAL;
    }

out:
    k_sem_give(&sbuf->lock);

    return err;
}

int thingset_storage_save()
{
    int err = 0;

    if (!nvs_initialized) {
        int err = data_storage_init();
        if (err != 0) {
            return err;
        }
    }

    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
    k_sem_take(&sbuf->lock, K_FOREVER);

    *((uint16_t *)&sbuf->data[0]) = (uint16_t)CONFIG_THINGSET_STORAGE_DATA_VERSION;

    int len =
        thingset_export_subsets(&ts, sbuf->data + NVS_HEADER_SIZE, sbuf->size - NVS_HEADER_SIZE,
                                TS_SUBSET_NVM, THINGSET_BIN_IDS_VALUES);

    LOG_HEXDUMP_DBG(sbuf->data, len + NVS_HEADER_SIZE, "data to be saved");

    if (len > 0) {
        int ret = nvs_write(&fs, THINGSET_DATA_ID, sbuf->data, len + NVS_HEADER_SIZE);
        if (ret == len + NVS_HEADER_SIZE) {
            LOG_DBG("NVS data successfully stored");
        }
        else if (ret == 0) {
            LOG_DBG("NVS data unchanged");
        }
        else {
            LOG_ERR("NVS write error %d", ret);
            err = ret;
        }
    }
    else {
        LOG_ERR("Exporting data failed with ThingSet response code 0x%X", -len);
        err = -EINVAL;
    }

    k_sem_give(&sbuf->lock);

    return err;
}

void thingset_storage_save_queued()
{
    thingset_sdk_reschedule_work(&storage_work, K_NO_WAIT);
}

static void regular_storage_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    thingset_storage_save();

    if (IS_ENABLED(CONFIG_THINGSET_STORAGE_REGULAR)) {
        thingset_sdk_reschedule_work(dwork, K_HOURS(CONFIG_THINGSET_STORAGE_INTERVAL));
    }
}

static int thingset_storage_init(void)
{
    k_work_init_delayable(&storage_work, regular_storage_handler);

    if (IS_ENABLED(CONFIG_THINGSET_STORAGE_REGULAR)) {
        thingset_sdk_reschedule_work(&storage_work, K_HOURS(CONFIG_THINGSET_STORAGE_INTERVAL));
    }

    return 0;
}

SYS_INIT(thingset_storage_init, APPLICATION, THINGSET_INIT_PRIORITY_STORAGE);
