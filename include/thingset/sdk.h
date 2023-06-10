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

/*
 * ID ranges for this firmware:
 *
 * 0x01 - 0x0F: groups defined by the application
 * 0x10 - 0x1F: reserved for ThingSet
 * 0x20 - 0x2F: groups defined by ThingSet SDK
 * 0x30 - 0x37: subsets defined by ThingSet SDK
 * 0x38 - 0x3F: subsets defined by the application
 * 0x40 - 0xFF: items of groups (starting e.g. with 0x40 for group 0x04)
 * 0x100 - 0x1FF: items for _pub group
 */

/*
 * Groups / first layer data object IDs
 */
#define ID_ROOT 0x00

#define ID_LORAWAN   0x27
#define ID_WIFI      0x28
#define ID_DFU       0x2D
#define ID_LOG       0x2E
#define ID_REPORTING 0x2F

#define ID_EVENT   0x30
#define ID_LIVE    0x31
#define ID_SUMMARY 0x32

#define SUBSET_EVENT_PATH   "e"
#define SUBSET_LIVE_PATH    "mLive"
#define SUBSET_SUMMARY_PATH "mSummary"

/*
 * Subset definitions for reports and stored data
 */
#define SUBSET_NVM     (1U << 0) // data that should be stored in EEPROM
#define SUBSET_LIVE    (1U << 1) // live data for high bandwidth interfaces (e.g. UART, BLE)
#define SUBSET_SUMMARY (1U << 2) // summarized data for low bandwidth interfaces (e.g. LoRaWAN)
#define SUBSET_EVENT   (1U << 3) // data only published on events (e.g. received meter reading)
#define SUBSET_CTRL    (1U << 4) // control data sent and received via CAN

/*
 * The storage has to be initialized first, so that the configuration can be read by the SDK
 * and used by all other components (using default priority)
 */
#define THINGSET_INIT_PRIORITY_STORAGE 30
#define THINGSET_INIT_PRIORITY_SDK     40
#define THINGSET_INIT_PRIORITY_DEFAULT 60

extern bool pub_events_enable;

extern bool pub_live_data_enable;
extern uint32_t pub_live_data_period;

extern bool pub_reports_enable;
extern uint32_t pub_reports_period;

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
