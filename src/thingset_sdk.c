/*
 * Copyright (c) 2022 Martin Jäger
 */

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/rand32.h>
#include <zephyr/sys/crc.h>

#include <stdio.h>
#include <string.h>

#include "thingset/sdk.h"
#include "thingset/storage.h"

LOG_MODULE_REGISTER(thingset_sdk, CONFIG_LOG_DEFAULT_LEVEL);

/*
 * The ThingSet node ID is an EUI-64 stored as upper-case hex string. It is also used as the
 * DevEUI for LoRaWAN. If available, it should be generated from a MAC address.
 *
 * Further information regarding EUI-64:
 * https://standards.ieee.org/wp-content/uploads/import/documents/tutorials/eui.pdf
 */
char node_id[17];
uint8_t eui64[8];

/* buffer should be word-aligned e.g. for hardware CRC calculations */
static uint8_t buf_data[CONFIG_THINGSET_SHARED_TX_BUF_SIZE] __aligned(sizeof(int));

static struct shared_buffer sbuf = {
    .data = buf_data,
    .size = sizeof(buf_data),
};

static const char firmware_version[] = FIRMWARE_VERSION_ID;

bool pub_events_enable = IS_ENABLED(CONFIG_THINGSET_PUB_LIVE_DATA_DEFAULT);

bool pub_live_data_enable = IS_ENABLED(CONFIG_THINGSET_PUB_LIVE_DATA_DEFAULT);
uint32_t pub_live_data_period = CONFIG_THINGSET_PUB_LIVE_DATA_PERIOD_DEFAULT;

bool pub_reports_enable = IS_ENABLED(CONFIG_THINGSET_PUB_REPORTS_DEFAULT);
uint32_t pub_reports_period = CONFIG_THINGSET_PUB_REPORTS_PERIOD_DEFAULT;

struct ts_context ts;

/* clang-format off */

TS_ADD_ITEM_STRING(0x1D, "cNodeID", node_id, sizeof(node_id),
    ID_ROOT, TS_ANY_R | TS_MKR_W, SUBSET_NVM);

TS_ADD_GROUP(ID_DEVICE, "Device", TS_NO_CALLBACK, ID_ROOT);

TS_ADD_ITEM_STRING(0x40, "cFirmwareVersion", firmware_version, 0,
    ID_DEVICE, TS_ANY_R, 0);

TS_ADD_SUBSET(ID_EVENT, SUBSET_EVENT_PATH, SUBSET_EVENT, ID_ROOT, TS_ANY_RW);
TS_ADD_SUBSET(ID_LIVE, SUBSET_LIVE_PATH, SUBSET_LIVE, ID_ROOT, TS_ANY_RW);
TS_ADD_SUBSET(ID_REPORT, SUBSET_REPORT_PATH, SUBSET_REPORT, ID_ROOT, TS_ANY_RW);

TS_ADD_GROUP(ID_PUB, "_pub", TS_NO_CALLBACK, ID_ROOT);

TS_ADD_GROUP(0x100, SUBSET_EVENT_PATH, NULL, ID_PUB);
TS_ADD_ITEM_BOOL(0x101, "sEnable", &pub_events_enable, 0x100, TS_ANY_RW, SUBSET_NVM);

TS_ADD_GROUP(0x110, SUBSET_LIVE_PATH, NULL, ID_PUB);
TS_ADD_ITEM_BOOL(0x111, "sEnable", &pub_live_data_enable, 0x110, TS_ANY_RW, SUBSET_NVM);
TS_ADD_ITEM_UINT32(0x112, "sPeriod_s", &pub_live_data_period, 0x110, TS_ANY_RW, SUBSET_NVM);

TS_ADD_GROUP(0x120, SUBSET_REPORT_PATH, NULL, ID_PUB);
TS_ADD_ITEM_BOOL(0x121, "sEnable", &pub_reports_enable, 0x120, TS_ANY_RW, SUBSET_NVM);
TS_ADD_ITEM_UINT32(0x122, "sPeriod_s", &pub_reports_period, 0x120, TS_ANY_RW, SUBSET_NVM);

/* clang-format on */

#ifdef CONFIG_THINGSET_GENERATE_NODE_ID
/*
 * Requirement: Generate a 64-bit ID from the 96-bit STM32 CPUID with very low
 * probability of collisions in a reproducible way (not based on random number).
 *
 * Approach: Calculate the CRC32 value over the first 64 bits and the last 64 bits of the 96-bit
 * chip ID and concatenate the results to a new 64-bit value. This takes into account all bytes of
 * the unique ID to maximize the entropy.
 */
static void generate_device_eui()
{
    /* zero-initialize buffer for predictable results for MCUs with device ID < 12 bytes */
    uint8_t buf[12] = { 0 };
    uint32_t crc;

    BUILD_ASSERT(sizeof(eui64) == 8);

#ifndef CONFIG_BOARD_NATIVE_POSIX
    hwinfo_get_device_id(buf, sizeof(buf));
#else
    /* hwinfo is not available in native_posix, so we use random data instead */
    for (int i = 0; i < sizeof(buf); i++) {
        buf[i] = sys_rand32_get() & 0xFF;
    }
#endif

    crc = crc32_ieee(buf, 8);
    memcpy(eui64, &crc, 4);
    crc = crc32_ieee(buf + 4, 8);
    memcpy(eui64 + 4, &crc, 4);

    /* set U/L bit to 0 for locally administered (not globally unique) EUIs */
    eui64[0] &= ~(1U << 1);

    snprintf(node_id, sizeof(node_id), "%.2X%.2X%.2X%.2X%.2X%.2X%.2X%.2X", eui64[0], eui64[1],
             eui64[2], eui64[3], eui64[4], eui64[5], eui64[6], eui64[7]);

    LOG_INF("ThingSet Node ID (EUI-64): %s", node_id);
}
#endif /* CONFIG_THINGSET_GENERATE_NODE_ID */

struct shared_buffer *thingset_sdk_shared_buffer(void)
{
    return &sbuf;
}

static int thingset_sdk_init(const struct device *dev)
{
    ARG_UNUSED(dev);

    k_sem_init(&sbuf.lock, 1, 1);

    ts_init_global(&ts);

#ifdef CONFIG_THINGSET_GENERATE_NODE_ID
    generate_device_eui();
#endif

#ifdef CONFIG_THINGSET_STORAGE
    thingset_storage_load();
    ts_set_update_callback(&ts, SUBSET_NVM, thingset_storage_save);
#endif

    return 0;
}

SYS_INIT(thingset_sdk_init, APPLICATION, THINGSET_INIT_PRIORITY_SDK);
