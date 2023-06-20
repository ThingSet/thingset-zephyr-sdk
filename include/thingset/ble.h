/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_BLE_H_
#define THINGSET_BLE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <thingset.h>
#include <thingset/sdk.h>

/**
 * Send ThingSet report to Bluetooth Central.
 *
 * @param path Path to subset or group that should be reported
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_ble_send_report(const char *path);

/**
 * Send ThingSet message (response or report) to Bluetooth Central.
 *
 * @param buf Buffer with ThingSet message (w/o SLIP characters)
 * @param len Length of message
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_ble_send(const uint8_t *buf, size_t len);

/**
 * Set custom callback for received data.
 *
 * If this callback is set, ThingSet messages are not processed automatically anymore, but
 * forwarded through the callback.
 */
void thingset_ble_set_rx_callback(thingset_sdk_rx_callback_t rx_cb);

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_BLE_H_ */
