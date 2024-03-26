/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

#include <stdio.h>

LOG_MODULE_REGISTER(thingset_storage_eeprom, CONFIG_THINGSET_SDK_LOG_LEVEL);

#if DT_NODE_EXISTS(DT_CHOSEN(thingset_eeprom))
#define EEPROM_DEVICE_NODE DT_CHOSEN(thingset_eeprom)
#else
#define EEPROM_DEVICE_NODE DT_NODELABEL(eeprom)
#endif

/* Testing showed that reading from the EEPROM resulted in corrupted data under certain conditions
 * if large amounts of data (>8 kB) were read at once. The STM32 I2C driver has a max chunk length
 * of 255 bytes, so the chunk size here should be smaller than that.
 */
#define MAX_READ_SIZE 128

struct thingset_eeprom_header
{
    uint16_t version;
    uint16_t data_len;
    uint32_t crc;
} __packed;

static struct k_work_delayable storage_work;

static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_DEVICE_NODE);

int thingset_storage_load()
{
    struct thingset_eeprom_header header;
    int err;

    if (!device_is_ready(eeprom_dev)) {
        LOG_ERR("EEPROM device not ready");
        return -ENODEV;
    }

    err = eeprom_read(eeprom_dev, 0, &header, sizeof(header));
    if (err != 0) {
        LOG_ERR("EEPROM read error %d", err);
        return err;
    }

    LOG_DBG("EEPROM header: ver %d, len %d, CRC %.8x", header.version, header.data_len, header.crc);

    if (header.version == 0xFFFF && header.data_len == 0xFFFF && header.crc == 0xFFFFFFFF) {
        LOG_DBG("EEPROM empty");
        return 0;
    }
    else if (header.version != CONFIG_THINGSET_STORAGE_DATA_VERSION) {
        LOG_WRN("EEPROM data ignored due to version mismatch: %d", header.version);
        return -EINVAL;
    }

    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();

    k_sem_take(&sbuf->lock, K_FOREVER);

    if (header.data_len > sbuf->size) {
#ifdef CONFIG_THINGSET_STORAGE_EEPROM_PROGRESSIVE_IMPORT_EXPORT
        uint32_t calculated_crc = 0x0;
        uint32_t last_id = 0;
        size_t processed_size = 0;
        size_t total_read_size = sizeof(header);
        size_t len = header.data_len;
        size_t read_offset = 0;
        do {
            int size = len > sbuf->size ? sbuf->size : len;
            int num_chunks = DIV_ROUND_UP(size, MAX_READ_SIZE);
            int remaining_bytes = size;
            read_offset = total_read_size;
            for (int i = 0; i < num_chunks; i++) {
                size_t read_size =
                    remaining_bytes > MAX_READ_SIZE ? MAX_READ_SIZE : remaining_bytes;
                LOG_DBG("Reading %d bytes starting at offset %d", read_size, read_offset);
                err =
                    eeprom_read(eeprom_dev, read_offset, &sbuf->data[i * MAX_READ_SIZE], read_size);
                if (err) {
                    LOG_ERR("Error %d reading EEPROM.", -err);
                    break;
                }
                read_offset += read_size;
                remaining_bytes -= read_size;
            }

            err =
                thingset_import_data_progressively(&ts, sbuf->data, size, THINGSET_BIN_IDS_VALUES,
                                                   THINGSET_WRITE_MASK, &last_id, &processed_size);
            calculated_crc = crc32_ieee_update(calculated_crc, sbuf->data, processed_size);
            total_read_size += processed_size;
            len -= processed_size;
        } while (len > 0 && err > 0);
        LOG_DBG("Finished processing %d bytes; calculated CRC %.8x",
                total_read_size - sizeof(header), calculated_crc);
        if (!err) {
            thingset_import_data_progressively_end(&ts);
        }

        if (calculated_crc == header.crc) {
            if (!err) {
                LOG_DBG("EEPROM read and data successfully updated");
            }
            else {
                LOG_ERR("Importing data failed with ThingSet response code 0x%X", -err);
                err = -EINVAL;
            }
        }
        else {
            LOG_ERR("EEPROM data CRC invalid, expected 0x%x and data_len %d", header.crc, len);
            err = -EINVAL;
        }

        goto out;
#else
        LOG_ERR("EEPROM buffer too small (%d bytes required)", header.data_len);
        err = -ENOMEM;
        goto out;
#endif /* CONFIG_THINGSET_STORAGE_EEPROM_PROGRESSIVE_IMPORT_EXPORT */
    }
    else {
        err = eeprom_read(eeprom_dev, sizeof(header), sbuf->data, header.data_len);
        if (err != 0) {
            LOG_ERR("EEPROM read failed: %d", err);
            goto out;
        }

        if (crc32_ieee(sbuf->data, header.data_len) == header.crc) {
            int status = thingset_import_data(&ts, sbuf->data, header.data_len, THINGSET_WRITE_MASK,
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
            LOG_ERR("EEPROM data CRC invalid, expected 0x%x and data_len %d", header.crc,
                    header.data_len);
            err = -EINVAL;
        }
    }

out:
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

    struct thingset_eeprom_header header = { .version = CONFIG_THINGSET_STORAGE_DATA_VERSION };

#ifdef CONFIG_THINGSET_STORAGE_EEPROM_PROGRESSIVE_IMPORT_EXPORT
    LOG_DBG("Initialising with buffer of size %d", sbuf->size);

    int rtn;
    int i = 0;
    size_t size;
    size_t total_size = sizeof(header);
    uint32_t crc = 0x0;
    do {
        rtn = thingset_export_subsets_progressively(&ts, sbuf->data, sbuf->size, TS_SUBSET_NVM,
                                                    THINGSET_BIN_IDS_VALUES, &i, &size);
        if (rtn < 0) {
            LOG_ERR("ThingSet data export error 0x%x", -rtn);
            err = -EINVAL;
            break;
        }
        crc = crc32_ieee_update(crc, sbuf->data, size);
        LOG_DBG("Writing %d bytes to EEPROM", size);
        err = eeprom_write(eeprom_dev, total_size, sbuf->data, size);
        if (err) {
            LOG_ERR("EEPROM write error %d", err);
            break;
        }
        total_size += size;
    } while (rtn > 0 && err == 0);
    if (!err) {
        total_size -= sizeof(header);
        LOG_DBG("Wrote a total of %d bytes comprising %d items with checksum %.8x; writing "
                "header",
                total_size, i, crc);

        /* now write the header */
        header.data_len = (uint16_t)total_size;
        header.crc = crc;
        err = eeprom_write(eeprom_dev, 0, &header, sizeof(header));
        LOG_DBG("EEPROM data successfully stored");
    }
    else {
        LOG_ERR("EEPROM write error %d", -err);
        err = -EINVAL;
    }

    goto out;
#else
    int len = thingset_export_subsets(&ts, sbuf->data, sbuf->size, TS_SUBSET_NVM,
                                      THINGSET_BIN_IDS_VALUES);
    if (len > 0) {
        uint32_t crc = crc32_ieee(sbuf->data, len);

        header.data_len = (uint16_t)len;
        header.crc = crc;

        LOG_DBG("EEPROM header: ver %d, len %d, CRC %.8x", CONFIG_THINGSET_STORAGE_DATA_VERSION,
                len, crc);

        err = eeprom_write(eeprom_dev, 0, &header, sizeof(header));
        if (err != 0) {
            LOG_DBG("Failed to write EEPROM header: %d", err);
            goto out;
        }

        err = eeprom_write(eeprom_dev, sizeof(header), sbuf->data, len);
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
#endif /* CONFIG_THINGSET_STORAGE_EEPROM_PROGRESSIVE_IMPORT_EXPORT */
out:
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
