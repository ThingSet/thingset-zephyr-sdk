/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_SDK_H_
#define THINGSET_SDK_H_

#include <zephyr/kernel.h>

#include <thingset.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 * The following table gives an overview of the IDs used by the ThingSet protocol and SDK and
 * the ranges freely available for the application.
 *
 * | ID range      | Defined in    | Purpose                                           |
 * |:-------------:|:-------------:|---------------------------------------------------|
 * | 0x01 - 0x0F   | Application   | Application-specific groups or items              |
 * | 0x10 - 0x1F   | ThingSet core | Part of the core ThingSet specification           |
 * | 0x20 - 0x2F   | ThingSet SDK  | Groups for Thingset SDK subsystems                |
 * | 0x30 - 0x37   | ThingSet SDK  | Subsets defined by ThingSet SDK                   |
 * | 0x38 - 0x3F   | Application   | Subsets defined by the application                |
 * | 0x40 - 0x1FF  | Application   | Available for custom application-specific objects |
 * | 0x200 - 0x2FF | ThingSet SDK  | Sub-objects of above groups and related overlays  |
 * | 0x300 - 0x37F | ThingSet SDK  | Sub-objects of _Reporting overlay                 |
 */

/* IDs from ThingSet node library */
#define TS_ID_ROOT        THINGSET_ID_ROOT
#define TS_ID_TIME        THINGSET_ID_TIME
#define TS_ID_IDS         THINGSET_ID_IDS
#define TS_ID_PATHS       THINGSET_ID_PATHS
#define TS_ID_METADATAURL THINGSET_ID_METADATAURL
#define TS_ID_NODEID      THINGSET_ID_NODEID
#define TS_ID_NODENAME    0x1E
#define TS_ID_EUI         0x1C

/* Authentication */
#define TS_ID_AUTH       0x20
#define TS_ID_AUTH_TOKEN 0x200

/* LoRaWAN group items */
#define TS_ID_LORAWAN           0x27
#define TS_ID_LORAWAN_DEV_EUI   0x270
#define TS_ID_LORAWAN_JOIN_EUI  0x271
#define TS_ID_LORAWAN_APP_KEY   0x272
#define TS_ID_LORAWAN_DEV_NONCE 0x273

/* Networking group items */
#define TS_ID_NET                      0x28
#define TS_ID_NET_WIFI_SSID            0x280
#define TS_ID_NET_WIFI_PSK             0x281
#define TS_ID_NET_IPV4                 0x282
#define TS_ID_NET_IPV6                 0x283
#define TS_ID_NET_WEBSOCKET_HOST       0x284
#define TS_ID_NET_WEBSOCKET_PORT       0x285
#define TS_ID_NET_WEBSOCKET_USE_TLS    0x286
#define TS_ID_NET_WEBSOCKET_AUTH_TOKEN 0x287
#define TS_ID_NET_CAN_NODE_ADDR        0x28C

/* Device Firmware Upgrade group items */
#define TS_ID_DFU 0x2D

/* Log group items */
#define TS_ID_LOG               0x2E
#define TS_ID_LOG_TIME          0x2E0
#define TS_ID_LOG_MESSAGE       0x2E1
#define TS_ID_LOG_MODULE        0x2E2
#define TS_ID_LOG_LEVEL         0x2E3
#define TS_ID_REP_LOG           0x2E9
#define TS_ID_REP_LOG_SELF      0x2EA
#define TS_ID_REP_LOG_ENABLE    0x2EB
#define TS_ID_REP_LOG_MAX_LEVEL 0x2EC

/* _Reporting overlay top-level object */
#define TS_ID_REPORTING 0x2F

/* Subsets defined by SDK */
#define TS_NAME_SUBSET_LIVE   "mLive"
#define TS_ID_SUBSET_LIVE     0x31
#define TS_ID_REP_LIVE        0x310
#define TS_ID_REP_LIVE_ENABLE 0x311
#define TS_ID_REP_LIVE_PERIOD 0x312

#define TS_NAME_SUBSET_SUMMARY   "mSummary"
#define TS_ID_SUBSET_SUMMARY     0x32
#define TS_ID_REP_SUMMARY        0x320
#define TS_ID_REP_SUMMARY_ENABLE 0x321
#define TS_ID_REP_SUMMARY_PERIOD 0x322

/** Data that should be stored in EEPROM or Flash */
#define TS_SUBSET_NVM (1U << 0)
/** Live data for high bandwidth interfaces (e.g. UART, BLE) */
#define TS_SUBSET_LIVE (1U << 1)
/** Summarized data for low bandwidth interfaces like LoRaWAN */
#define TS_SUBSET_SUMMARY (1U << 2)

/*
 * The storage has to be initialized first, so that the configuration can be read by the SDK
 * and used by all other components (using default priority)
 */
#define THINGSET_INIT_PRIORITY_STORAGE 30
#define THINGSET_INIT_PRIORITY_SDK     40
#define THINGSET_INIT_PRIORITY_DEFAULT 60

extern char node_id[17];
extern uint8_t eui64[8];

extern bool pub_events_enable;

extern bool live_reporting_enable;
extern uint32_t live_reporting_period;

extern bool summary_reporting_enable;
extern uint32_t summary_reporting_period;

extern struct thingset_context ts;

struct shared_buffer
{
    struct k_sem lock;
    uint8_t *data;
    const size_t size;
    size_t pos;
};

/**
 * Callback typedef for received ThingSet messages in different interfaces
 */
typedef void (*thingset_sdk_rx_callback_t)(const uint8_t *buf, size_t len);

/**
 * Get TX buffer that can be shared between different ThingSet interfaces
 *
 * @returns Pointer to shared_buffer instance
 */
struct shared_buffer *thingset_sdk_shared_buffer(void);

/**
 * Add delayable work to the common ThingSet SDK work queue. This should be used to offload
 * processing of incoming requests and sending out reports.
 */
int thingset_sdk_reschedule_work(struct k_work_delayable *dwork, k_timeout_t delay);

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_SDK_H_ */
