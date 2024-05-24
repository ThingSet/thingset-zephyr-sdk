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

struct thingset_eeprom_header
{
    uint16_t version;
    uint16_t data_len;
    uint32_t crc;
} __packed;

static const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_DEVICE_NODE);

static int thingset_eeprom_load(off_t offset)
{
    struct thingset_eeprom_header header;
    int err;

    err = eeprom_read(eeprom_dev, offset, &header, sizeof(header));
    if (err != 0) {
        LOG_ERR("EEPROM read error %d", err);
        return err;
    }

    LOG_INF("EEPROM load: ver %d, len %d, CRC 0x%.8x", header.version, header.data_len, header.crc);

    if ((header.version == 0xFFFF && header.data_len == 0xFFFF && header.crc == 0xFFFFFFFF)
        || (header.version == 0U && header.data_len == 0U && header.crc == 0U))
    {
        LOG_INF("EEPROM empty, keeping default values for data objects");
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
        size_t chunk_offset = 0;
        do {
            int size = len > sbuf->size ? sbuf->size : len;
            int num_chunks = DIV_ROUND_UP(size, CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE);
            int remaining_bytes = size;
            chunk_offset = total_read_size;
            for (int i = 0; i < num_chunks; i++) {
                size_t read_size = remaining_bytes > CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE
                                       ? CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE
                                       : remaining_bytes;
                LOG_DBG("Reading %d bytes starting at offset 0x%.4x", read_size, chunk_offset);
                err = eeprom_read(eeprom_dev, offset + chunk_offset,
                                  &sbuf->data[i * CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE],
                                  read_size);
                if (err) {
                    LOG_ERR("Error %d reading EEPROM.", -err);
                    break;
                }
                chunk_offset += read_size;
                remaining_bytes -= read_size;
            }

            err =
                thingset_import_data_progressively(&ts, sbuf->data, size, THINGSET_BIN_IDS_VALUES,
                                                   THINGSET_WRITE_MASK, &last_id, &processed_size);
            calculated_crc = crc32_ieee_update(calculated_crc, sbuf->data, processed_size);
            LOG_DBG("Updated CRC over %d bytes: 0x%.8x", processed_size, calculated_crc);
            total_read_size += processed_size;
            len -= processed_size;
        } while (len > 0 && err > 0);

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
            LOG_ERR("EEPROM data CRC invalid, expected 0x%.8x and data_len %d", header.crc,
                    header.data_len);
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
        err = eeprom_read(eeprom_dev, offset + sizeof(header), sbuf->data, header.data_len);
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
            LOG_ERR("EEPROM data CRC invalid, expected 0x%.8x and data_len %d", header.crc,
                    header.data_len);
            err = -EINVAL;
        }
    }

out:
    k_sem_give(&sbuf->lock);

    return err;
}

static int thingset_eeprom_save(off_t offset, size_t useable_size)
{
    int err = 0;

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
    uint8_t read_back[CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE];
    size_t chunk_offset = 0;
    do {
        rtn = thingset_export_subsets_progressively(&ts, sbuf->data, sbuf->size, TS_SUBSET_NVM,
                                                    THINGSET_BIN_IDS_VALUES, &i, &size);
        if (rtn < 0) {
            LOG_ERR("ThingSet data export error 0x%x", -rtn);
            err = -EINVAL;
            break;
        }
        crc = crc32_ieee_update(crc, sbuf->data, size);
        LOG_DBG("Writing %d bytes to EEPROM, updated CRC: 0x%.8x", size, crc);

        int num_chunks = DIV_ROUND_UP(size, CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE);
        int remaining_bytes = size;
        chunk_offset = total_size;
        for (int i = 0; i < num_chunks; i++) {
            size_t write_size = remaining_bytes > CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE
                                    ? CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE
                                    : remaining_bytes;
            for (int j = 0; j < CONFIG_THINGSET_STORAGE_LOAD_ATTEMPTS; j++) {
                err = eeprom_write(eeprom_dev, offset + chunk_offset,
                                   &sbuf->data[i * CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE],
                                   write_size);
                if (err) {
                    LOG_DBG("Write error %d", -err);
                    continue;
                }
                err = eeprom_read(eeprom_dev, offset + chunk_offset, &read_back, write_size);
                if (err) {
                    LOG_DBG("Read error %d", -err);
                    continue;
                }
                err = memcmp(&sbuf->data[i * CONFIG_THINGSET_STORAGE_EEPROM_CHUNK_SIZE], read_back,
                             write_size);
                if (err) {
                    LOG_DBG("Verify error %d", err);
                    continue;
                }
                else {
                    break;
                }
            }
            if (err) {
                LOG_ERR("Error %d writing EEPROM.", -err);
                break;
            }
            chunk_offset += write_size;
        }
        total_size += size;
    } while (rtn > 0 && err == 0);
    if (!err) {
        total_size -= sizeof(header);

        /* now write the header */
        header.data_len = (uint16_t)total_size;
        header.crc = crc;
        err = eeprom_write(eeprom_dev, offset, &header, sizeof(header));

        LOG_INF("EEPROM save: ver %d, len %d, CRC 0x%.8x", CONFIG_THINGSET_STORAGE_DATA_VERSION,
                total_size, crc);
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

        LOG_INF("EEPROM save: ver %d, len %d, CRC 0x%.8x", CONFIG_THINGSET_STORAGE_DATA_VERSION,
                len, crc);

        err = eeprom_write(eeprom_dev, offset, &header, sizeof(header));
        if (err != 0) {
            LOG_DBG("Failed to write EEPROM header: %d", err);
            goto out;
        }

        err = eeprom_write(eeprom_dev, offset + sizeof(header), sbuf->data, len);
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

int thingset_storage_load()
{
    if (!device_is_ready(eeprom_dev)) {
        LOG_ERR("EEPROM device not ready");
        return -ENODEV;
    }

#ifdef CONFIG_THINGSET_STORAGE_EEPROM_DUPLICATE
    size_t eeprom_size = eeprom_get_size(eeprom_dev);
    int err = thingset_eeprom_load(0);
    if (err != 0) {
        /* first data section invalid, try second one */
        err = thingset_eeprom_load(eeprom_size / 2);
    }
    return err;
#else
    return thingset_eeprom_load(0);
#endif
}

int thingset_storage_save()
{
    if (!device_is_ready(eeprom_dev)) {
        LOG_ERR("EEPROM device not ready");
        return -ENODEV;
    }

    size_t eeprom_size = eeprom_get_size(eeprom_dev);

#ifdef CONFIG_THINGSET_STORAGE_EEPROM_DUPLICATE
    int err = thingset_eeprom_save(0, eeprom_size / 2);
    if (err != 0) {
        return err;
    }
    return thingset_eeprom_save(eeprom_size / 2, eeprom_size / 2);
#else
    return thingset_eeprom_save(0, eeprom_size);
#endif
}
