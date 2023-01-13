/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "thingset/ble.h"
#include "thingset/sdk.h"
#include "thingset/serial.h"

void serial_rx_callback(const uint8_t *buf, size_t len)
{
    thingset_ble_tx(buf, len);
}

void ble_rx_callback(const uint8_t *buf, size_t len)
{
    thingset_serial_tx(buf, len);
}

void main(void)
{
    thingset_serial_set_rx_callback(serial_rx_callback);
    thingset_ble_set_rx_callback(ble_rx_callback);
}
