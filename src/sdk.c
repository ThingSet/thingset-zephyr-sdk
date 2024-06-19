/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/crc.h>

#include <stdio.h>
#include <string.h>

#include <thingset.h>
#include <thingset/sdk.h>

#ifdef CONFIG_THINGSET_PID_EUI
#include <unistd.h>
#endif

LOG_MODULE_REGISTER(thingset_sdk, CONFIG_THINGSET_SDK_LOG_LEVEL);

/*
 * The ThingSet node ID is an EUI-64 stored as upper-case hex string. It is also used as the
 * DevEUI for LoRaWAN. If available, it should be generated from a MAC address.
 *
 * Further information regarding EUI-64:
 * https://standards.ieee.org/wp-content/uploads/import/documents/tutorials/eui.pdf
 */
char node_id[17];
uint8_t eui64[8];

char node_name[] = CONFIG_THINGSET_NODE_NAME;

/* buffer should be word-aligned e.g. for hardware CRC calculations */
static uint8_t buf_data[CONFIG_THINGSET_SHARED_TX_BUF_SIZE] __aligned(sizeof(int));

static struct shared_buffer sbuf = {
    .data = buf_data,
    .size = sizeof(buf_data),
};

K_THREAD_STACK_DEFINE(thread_stack_area, CONFIG_THINGSET_SDK_THREAD_STACK_SIZE);

/*
 * The services need a dedicated work queue, as the LoRaWAN stack uses the system
 * work queue and gets blocked if other LoRaWAN messages are sent and processed from
 * the system work queue in parallel.
 */
static struct k_work_q thingset_workq;

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
bool live_reporting_enable = IS_ENABLED(CONFIG_THINGSET_REPORTING_LIVE_ENABLE_PRESET);
uint32_t live_reporting_period = CONFIG_THINGSET_REPORTING_LIVE_PERIOD_PRESET;
#endif

#ifdef CONFIG_THINGSET_SUBSET_SUMMARY_METRICS
bool summary_reporting_enable = IS_ENABLED(CONFIG_THINGSET_REPORTING_SUMMARY_ENABLE_PRESET);
uint32_t summary_reporting_period = CONFIG_THINGSET_REPORTING_SUMMARY_PERIOD_PRESET;
#endif

struct thingset_context ts;

THINGSET_ADD_ITEM_STRING(TS_ID_ROOT, THINGSET_ID_NODEID, "pNodeID", node_id, sizeof(node_id),
                         THINGSET_ANY_R | THINGSET_MFR_W, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_ROOT, TS_ID_NODENAME, "pNodeName", node_name, sizeof(node_name),
                         THINGSET_ANY_R | THINGSET_MFR_W, TS_SUBSET_NVM);

#if defined(CONFIG_THINGSET_WIFI) || defined(CONFIG_THINGSET_WEBSOCKET) \
    || (defined(CONFIG_THINGSET_CAN) && !defined(CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES))
THINGSET_ADD_GROUP(TS_ID_ROOT, TS_ID_NET, "Networking", THINGSET_NO_CALLBACK);
#endif

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
THINGSET_ADD_SUBSET(TS_ID_ROOT, TS_ID_SUBSET_LIVE, TS_NAME_SUBSET_LIVE, TS_SUBSET_LIVE,
                    THINGSET_ANY_RW);
#endif

#ifdef CONFIG_THINGSET_SUBSET_SUMMARY_METRICS
THINGSET_ADD_SUBSET(TS_ID_ROOT, TS_ID_SUBSET_SUMMARY, TS_NAME_SUBSET_SUMMARY, TS_SUBSET_SUMMARY,
                    THINGSET_ANY_RW);
#endif

THINGSET_ADD_GROUP(TS_ID_ROOT, TS_ID_REPORTING, "_Reporting", THINGSET_NO_CALLBACK);

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
THINGSET_ADD_GROUP(TS_ID_REPORTING, TS_ID_REP_LIVE, TS_NAME_SUBSET_LIVE, NULL);
THINGSET_ADD_ITEM_BOOL(TS_ID_REP_LIVE, TS_ID_REP_LIVE_ENABLE, "sEnable", &live_reporting_enable,
                       THINGSET_ANY_RW, TS_SUBSET_NVM);
THINGSET_ADD_ITEM_UINT32(TS_ID_REP_LIVE, TS_ID_REP_LIVE_PERIOD, "sPeriod_s", &live_reporting_period,
                         THINGSET_ANY_RW, TS_SUBSET_NVM);
#endif

#ifdef CONFIG_THINGSET_SUBSET_SUMMARY_METRICS
THINGSET_ADD_GROUP(TS_ID_REPORTING, TS_ID_REP_SUMMARY, TS_NAME_SUBSET_SUMMARY, NULL);
THINGSET_ADD_ITEM_BOOL(TS_ID_REP_SUMMARY, TS_ID_REP_SUMMARY_ENABLE, "sEnable",
                       &summary_reporting_enable, THINGSET_ANY_RW, TS_SUBSET_NVM);
THINGSET_ADD_ITEM_UINT32(TS_ID_REP_SUMMARY, TS_ID_REP_SUMMARY_PERIOD, "sPeriod_s",
                         &summary_reporting_period, THINGSET_ANY_RW, TS_SUBSET_NVM);
#endif

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

#ifndef CONFIG_THINGSET_PID_EUI
    /* hwinfo is not available in native_posix, so we use random data instead */
    for (int i = 0; i < sizeof(buf); i++) {
        buf[i] = sys_rand32_get() & 0xFF;
    }
#else
    /* hwinfo is not available in native_posix, so we take task PID instead */
    int pid = getpid();

    snprintk(buf, sizeof(buf), "%X", pid);
#endif

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

int thingset_sdk_reschedule_work(struct k_work_delayable *dwork, k_timeout_t delay)
{
    return k_work_reschedule_for_queue(&thingset_workq, dwork, delay);
}

static int thingset_sdk_init(void)
{
    k_sem_init(&sbuf.lock, 1, 1);

    k_work_queue_init(&thingset_workq);
    k_work_queue_start(&thingset_workq, thread_stack_area, K_THREAD_STACK_SIZEOF(thread_stack_area),
                       CONFIG_THINGSET_SDK_THREAD_PRIORITY, NULL);

    k_thread_name_set(&thingset_workq.thread, "thingset_sdk");

    thingset_init_global(&ts);

#ifdef CONFIG_THINGSET_GENERATE_NODE_ID
    generate_device_eui();
#endif

    return 0;
}

SYS_INIT(thingset_sdk_init, APPLICATION, THINGSET_INIT_PRIORITY_SDK);
