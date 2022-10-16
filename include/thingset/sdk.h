/*
 * Copyright (c) 2022 Martin Jäger
 */

#ifndef THINGSET_SDK_H_
#define THINGSET_SDK_H_

#include <thingset.h>

/*
 * ID ranges for this firmware:
 *
 * 0x01 - 0x04: reserved
 * 0x04 - 0x0F: common groups
 * 0x10 - 0x1F: reserved for ThingSet
 * 0x20 - 0x2F: common subsets
 * 0x30 - 0x3F: reserved for further groups
 * 0x40 - 0xFF: items of groups (starting e.g. with 0x40 for group 0x04)
 * 0x100 - 0x1FF: items for _pub group
 */

/*
 * Groups / first layer data object IDs
 */
#define ID_ROOT    0x00
#define ID_DEVICE  0x04
#define ID_LORAWAN 0x07
#define ID_WIFI    0x08

#define ID_EVENT  0x20
#define ID_LIVE   0x21
#define ID_REPORT 0x22
#define ID_PUB    0x3F

#define SUBSET_EVENT_PATH  "e"
#define SUBSET_LIVE_PATH   "mLive"
#define SUBSET_REPORT_PATH "mReport"

/*
 * Subset definitions for statements and publish/subscribe
 */
#define SUBSET_NVM    (1U << 0) // data that should be stored in EEPROM
#define SUBSET_LIVE   (1U << 1) // live data for high bandwidth interfaces (e.g. UART, BLE)
#define SUBSET_REPORT (1U << 2) // summarized data for low bandwidth interfaces (e.g. LoRaWAN)
#define SUBSET_EVENT  (1U << 3) // data only published on events (e.g. received meter reading)

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

extern struct ts_context ts;

#endif /* THINGSET_SDK_H_ */
