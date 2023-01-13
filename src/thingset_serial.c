/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "thingset/sdk.h"
#include "thingset/serial.h"

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(thingset_serial, CONFIG_LOG_DEFAULT_LEVEL);

#if DT_NODE_EXISTS(DT_CHOSEN(thingset_serial))
#define UART_DEVICE_NODE DT_CHOSEN(thingset_serial)
#else
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
#endif

static const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static char rx_buf[CONFIG_THINGSET_SERIAL_RX_BUF_SIZE];

static volatile size_t rx_buf_pos = 0;
static bool discard_buffer;

static struct k_sem command_flag; // used as an event to signal a received command
static struct k_sem rx_buf_mutex; // binary semaphore used as mutex in ISR context

static thingset_sdk_rx_callback_t rx_callback;

static struct ts_data_object *live_data_subset;

int thingset_serial_tx(const uint8_t *buf, size_t len)
{
    for (int i = 0; i < len; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }

    uart_poll_out(uart_dev, '\r');
    uart_poll_out(uart_dev, '\n');

    return 0;
}

void thingset_serial_pub_statement(struct ts_data_object *subset)
{
    if (subset != NULL) {
        struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
        k_sem_take(&tx_buf->lock, K_FOREVER);

        int len = ts_txt_statement(&ts, tx_buf->data, tx_buf->size, subset);
        thingset_serial_tx(tx_buf->data, len);

        k_sem_give(&tx_buf->lock);
    }
}

static void serial_process_command()
{
    if (rx_buf_pos > 0) {
        LOG_DBG("Received Request (%d bytes): %s", rx_buf_pos, rx_buf);

        if (rx_callback == NULL) {
            struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
            k_sem_take(&tx_buf->lock, K_FOREVER);

            int len = ts_process(&ts, (uint8_t *)rx_buf, rx_buf_pos, tx_buf->data, tx_buf->size);
            for (int i = 0; i < len; i++) {
                uart_poll_out(uart_dev, tx_buf->data[i]);
            }

            k_sem_give(&tx_buf->lock);

            uart_poll_out(uart_dev, '\r');
            uart_poll_out(uart_dev, '\n');
        }
        else {
            /* external processing (e.g. for gateway applications) */
            rx_callback(rx_buf, rx_buf_pos);
        }
    }

    // release buffer and start waiting for new commands
    rx_buf_pos = 0;
    k_sem_give(&rx_buf_mutex);
}

static void serial_rx_buf_put(uint8_t c)
{
    if (k_sem_take(&rx_buf_mutex, K_NO_WAIT) != 0) {
        // buffer not available: drop character
        discard_buffer = true;
        return;
    }

    // \r\n and \n are markers for line end, i.e. command end
    // we accept this at any time, even if the buffer is 'full', since
    // there is always one last character left for the \0
    if (c == '\n') {
        if (rx_buf_pos > 0 && rx_buf[rx_buf_pos - 1] == '\r') {
            rx_buf[--rx_buf_pos] = '\0';
        }
        else {
            rx_buf[rx_buf_pos] = '\0';
        }
        if (discard_buffer) {
            rx_buf_pos = 0;
            discard_buffer = false;
            k_sem_give(&rx_buf_mutex);
        }
        else {
            // start processing command and keep the rx_buf_mutex locked
            k_sem_give(&command_flag);
        }
        return;
    }
    // backspace allowed if there is something in the buffer already
    else if (rx_buf_pos > 0 && c == '\b') {
        rx_buf_pos--;
    }
    // Fill the buffer up to all but 1 character (the last character is reserved for '\0')
    // Characters beyond the size of the buffer are dropped.
    else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
        rx_buf[rx_buf_pos++] = c;
    }

    k_sem_give(&rx_buf_mutex);
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
/*
 * Read characters from stream until line end \n is detected, afterwards signal available command.
 */
static void serial_rx_cb(const struct device *dev, void *user_data)
{
    uint8_t c;

    if (!uart_irq_update(uart_dev)) {
        return;
    }

    while (uart_irq_rx_ready(uart_dev)) {
        uart_fifo_read(uart_dev, &c, 1);
        serial_rx_buf_put(c);
    }
}
#endif

void thingset_serial_set_rx_callback(thingset_sdk_rx_callback_t rx_cb)
{
    rx_callback = rx_cb;
}

static void thingset_serial_thread()
{
    if (!device_is_ready(uart_dev)) {
        // this may happen if a phone charger is used to power the device, which will
        // usually short the USB D+ and D- wires. In this case we just don't use this interface
        return;
    }

    k_sem_init(&command_flag, 0, 1);
    k_sem_init(&rx_buf_mutex, 1, 1);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    uart_irq_callback_user_data_set(uart_dev, serial_rx_cb, NULL);
    uart_irq_rx_enable(uart_dev);
#endif

    live_data_subset = ts_get_object_by_path(&ts, SUBSET_LIVE_PATH, sizeof(SUBSET_LIVE_PATH) - 1);

    int64_t pub_time = k_uptime_get();
    while (true) {
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
        k_timeout_t timeout = live_data_subset ? K_TIMEOUT_ABS_MS(pub_time) : K_FOREVER;
        if (k_sem_take(&command_flag, timeout) == 0) {
            serial_process_command();
        }
        else {
            // semaphore timed out (should happen exactly once every defined period)
            pub_time += 1000 * pub_live_data_period;
            if (pub_live_data_enable) {
                thingset_serial_pub_statement(live_data_subset);
            }
        }
#else /* Polling API */
        uint8_t c;
        while (uart_poll_in(uart_dev, &c) == 0) {
            serial_rx_buf_put(c);
        }
        if (k_sem_take(&command_flag, K_NO_WAIT) == 0) {
            serial_process_command();
        }
        if (k_uptime_get() >= pub_time) {
            pub_time += 1000 * pub_live_data_period;
            if (pub_live_data_enable) {
                thingset_serial_pub_statement(live_data_subset);
            }
        }
        k_sleep(K_MSEC(1));
#endif
    }
}

K_THREAD_DEFINE(thingset_serial, 1280, thingset_serial_thread, NULL, NULL, NULL, 6, 0, 1000);
