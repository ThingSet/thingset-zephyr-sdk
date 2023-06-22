/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "thingset/ble.h"
#include "thingset/sdk.h"
#include "thingset/serial.h"

#define LED0_NODE DT_ALIAS(led0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

void switch_led_off(struct k_timer *timer)
{
    if (device_is_ready(led.port)) {
        gpio_pin_set_dt(&led, 0);
    }
}

K_TIMER_DEFINE(led_off_timer, switch_led_off, NULL);

void flash_led()
{
    if (device_is_ready(led.port)) {
        gpio_pin_set_dt(&led, 1);
        k_timer_start(&led_off_timer, K_MSEC(100), K_NO_WAIT);
    }
}

void serial_rx_callback(const uint8_t *buf, size_t len)
{
    thingset_ble_send(buf, len);
    flash_led();
}

void ble_rx_callback(const uint8_t *buf, size_t len)
{
    thingset_serial_send(buf, len);
    flash_led();
}

int main(void)
{
    thingset_serial_set_rx_callback(serial_rx_callback);
    thingset_ble_set_rx_callback(ble_rx_callback);

    if (!device_is_ready(led.port)) {
        /* ignore LED if not available */
        return 0;
    }

    /* blink LED once to indicate start-up of the board */
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    k_sleep(K_SECONDS(1));
    gpio_pin_set_dt(&led, 0);

    return 0;
}
