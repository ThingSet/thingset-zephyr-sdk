/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

#include <stdio.h>

LOG_MODULE_REGISTER(thingset_storage_eeprom, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * EEPROM header bytes:
 * - 0-1: Data objects version number
 * - 2-3: Number of data bytes
 * - 4-7: CRC32
 *
 * Data starts from byte 8
 */
#define EEPROM_HEADER_SIZE 8

#if DT_NODE_EXISTS(DT_CHOSEN(thingset_eeprom))
#define EEPROM_DEVICE_NODE DT_CHOSEN(thingset_eeprom)
#else
#define EEPROM_DEVICE_NODE DT_NODELABEL(eeprom)
#endif

static struct k_work_delayable storage_work;

static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_DEVICE_NODE);

int thingset_storage_load()
{
    int err = 0;

    if (!device_is_ready(eeprom_dev)) {
        LOG_ERR("EEPROM device not ready");
        return -ENODEV;
    }

    uint8_t buf_header[EEPROM_HEADER_SIZE] = {};

    err = eeprom_read(eeprom_dev, 0, buf_header, EEPROM_HEADER_SIZE);
    if (err != 0) {
        LOG_ERR("EEPROM read error %d", err);
        return err;
    }

    uint16_t version = *((uint16_t *)&buf_header[0]);
    uint16_t len = *((uint16_t *)&buf_header[2]);
    uint32_t crc = *((uint32_t *)&buf_header[4]);

    LOG_DBG("EEPROM header: ver %d, len %d, CRC %.8x", version, len, crc);

    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
    k_sem_take(&sbuf->lock, K_FOREVER);

    if (version == CONFIG_THINGSET_STORAGE_DATA_VERSION && len <= sbuf->size) {

        err = eeprom_read(eeprom_dev, EEPROM_HEADER_SIZE, sbuf->data, len);

        if (crc32_ieee(sbuf->data, len) == crc) {
            int status = thingset_import_data(&ts, sbuf->data, len, THINGSET_WRITE_MASK,
                                              THINGSET_BIN_IDS_VALUES);
            if (status == 0) {
                LOG_DBG("EEPROM read and data successfully updated");
            }
            else {
                LOG_ERR("Importing data failed with ThingSet response code 0x%X", -status);
                err = -EINVAL;
            }
        }
        else {
            LOG_ERR("EEPROM data CRC invalid, expected 0x%x and data_len %d", crc, len);
            err = -EINVAL;
        }
    }
    else if (version == 0xFFFF && len == 0xFFFF && crc == 0xFFFFFFFF) {
        LOG_DBG("EEPROM empty");
    }
    else {
        LOG_WRN("EEPROM data ignored due to version mismatch: %d", version);
        err = -EINVAL;
    }

    k_sem_give(&sbuf->lock);

    return err;
}

int thingset_storage_save()
{
    int err;

    if (!device_is_ready(eeprom_dev)) {
        LOG_ERR("EEPROM device not ready");
        return -ENODEV;
    }

    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
    k_sem_take(&sbuf->lock, K_FOREVER);

    int len = thingset_export_subsets(&ts, sbuf->data + EEPROM_HEADER_SIZE,
                                      sbuf->size - EEPROM_HEADER_SIZE, TS_SUBSET_NVM,
                                      THINGSET_BIN_IDS_VALUES);
    if (len > 0) {
        uint32_t crc = crc32_ieee(sbuf->data + EEPROM_HEADER_SIZE, len);

        *((uint16_t *)&sbuf->data[0]) = (uint16_t)CONFIG_THINGSET_STORAGE_DATA_VERSION;
        *((uint16_t *)&sbuf->data[2]) = (uint16_t)(len);
        *((uint32_t *)&sbuf->data[4]) = crc;

        LOG_DBG("EEPROM header: ver %d, len %d, CRC %.8x", CONFIG_THINGSET_STORAGE_DATA_VERSION,
                len, crc);

        err = eeprom_write(eeprom_dev, 0, sbuf->data, len + EEPROM_HEADER_SIZE);
        if (err == 0) {
            LOG_DBG("EEPROM data successfully stored");
        }
        else {
            LOG_ERR("EEPROM write error %d", err);
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
