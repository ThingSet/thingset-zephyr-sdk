/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_SERIAL_H_
#define THINGSET_SERIAL_H_

#include <thingset.h>

#include "thingset/sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

void thingset_serial_pub_statement(struct ts_data_object *subset);

/**
 * Send ThingSet message (response or statement) to serial client.
 *
 * @param buf Buffer with ThingSet payload
 * @param len Length of payload inside the buffer
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_serial_tx(const uint8_t *buf, size_t len);

/**
 * Set custom callback for received data.
 *
 * If this callback is set, ThingSet messages are not processed automatically anymore, but
 * forwarded through the callback.
 */
void thingset_serial_set_rx_callback(thingset_sdk_rx_callback_t rx_cb);

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_SERIAL_H_ */
