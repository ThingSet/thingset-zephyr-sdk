/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/serial.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(thingset_serial, CONFIG_THINGSET_SDK_LOG_LEVEL);

#if DT_NODE_EXISTS(DT_CHOSEN(thingset_serial))
#define UART_DEVICE_NODE DT_CHOSEN(thingset_serial)
#else
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
#endif

static const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static char rx_buf[CONFIG_THINGSET_SERIAL_RX_BUF_SIZE];

static volatile size_t rx_buf_pos = 0;
static bool discard_buffer;

/* binary semaphore used as mutex in ISR context */
static struct k_sem rx_buf_lock;

static thingset_sdk_rx_callback_t rx_callback;

static struct k_work_delayable processing_work;
#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
static struct k_work_delayable reporting_work;
#endif

int thingset_serial_send(const uint8_t *buf, size_t len)
{
    if (!device_is_ready(uart_dev)) {
        return -ENODEV;
    }

    for (int i = 0; i < len; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }

#ifdef CONFIG_THINGSET_SERIAL_USE_CRC
    uint32_t crc = crc32_ieee(buf, len);
    uint8_t crc_str[11];
    snprintf(crc_str, sizeof(crc_str), " %08X#", crc);
    for (int i = 0; i < 10; i++) {
        uart_poll_out(uart_dev, crc_str[i]);
    }
#endif

    uart_poll_out(uart_dev, '\r');
    uart_poll_out(uart_dev, '\n');

    return 0;
}

int thingset_serial_send_report(const char *path)
{
    struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
    k_sem_take(&tx_buf->lock, K_FOREVER);

    int len =
        thingset_report_path(&ts, tx_buf->data, tx_buf->size, path, THINGSET_TXT_NAMES_VALUES);

    int ret = thingset_serial_send(tx_buf->data, len);

    k_sem_give(&tx_buf->lock);
    return ret;
}

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS

static void serial_regular_report_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    static int64_t pub_time;

    if (live_reporting_enable) {
        thingset_serial_send_report(TS_NAME_SUBSET_LIVE);
    }

    pub_time += 1000 * live_reporting_period;
    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(pub_time));
}

#endif

static void serial_process_msg_handler(struct k_work *work)
{
    if (rx_buf_pos > 0) {
        LOG_DBG("Received Request (%d bytes): %s", rx_buf_pos, rx_buf);

#ifdef CONFIG_THINGSET_SERIAL_USE_CRC
        if (rx_buf[rx_buf_pos - 1] == '#' && rx_buf_pos > 10) {
            /* message with checksum */
            rx_buf[--rx_buf_pos] = '\0';
            uint32_t crc_rx = strtoul(&rx_buf[rx_buf_pos - 8], NULL, 16);
            rx_buf_pos -= 9; /* strip CRC and white space */
            uint32_t crc_calc = crc32_ieee(rx_buf, rx_buf_pos);
            if (crc_rx != crc_calc) {
                LOG_WRN("Discarded message with bad CRC, expected %08X", crc_calc);
                goto out;
            }
            LOG_DBG("crc_rx: %08X, crc_calc: %08X", crc_rx, crc_calc);
        }
#endif /* CONFIG_THINGSET_SERIAL_USE_CRC */
#ifdef CONFIG_THINGSET_SERIAL_ENFORCE_CRC
        else {
            LOG_WRN("Discarded message without CRC");
            goto out;
        }
#endif /* CONFIG_THINGSET_SERIAL_ENFORCE_CRC */

        if (rx_callback == NULL) {
            struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
            k_sem_take(&tx_buf->lock, K_FOREVER);

            int len = thingset_process_message(&ts, (uint8_t *)rx_buf, rx_buf_pos, tx_buf->data,
                                               tx_buf->size);
            if (len > 0) {
                thingset_serial_send(tx_buf->data, len);
            }

            k_sem_give(&tx_buf->lock);
        }
        else {
            /* external processing (e.g. for gateway applications) */
            rx_callback(rx_buf, rx_buf_pos);
        }
    }

out:
    /* release buffer and start waiting for new commands */
    rx_buf_pos = 0;
    k_sem_give(&rx_buf_lock);
}

static void serial_rx_buf_put(uint8_t c)
{
    if (k_sem_take(&rx_buf_lock, K_NO_WAIT) != 0) {
        // buffer not available: drop character
        discard_buffer = true;
        return;
    }

    // \r\n and \n are markers for line end, i.e. request end
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
            k_sem_give(&rx_buf_lock);
        }
        else {
            // start processing request and keep the rx_buf_lock
            thingset_sdk_reschedule_work(&processing_work, K_NO_WAIT);
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

    k_sem_give(&rx_buf_lock);
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

static int thingset_serial_init()
{
    if (!device_is_ready(uart_dev)) {
        // this may happen if a phone charger is used to power the device, which will
        // usually short the USB D+ and D- wires. In this case we just don't use this interface
        return -ENODEV;
    }

    k_sem_init(&rx_buf_lock, 1, 1);

    k_work_init_delayable(&processing_work, serial_process_msg_handler);

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
    k_work_init_delayable(&reporting_work, serial_regular_report_handler);
#endif

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
    uart_irq_callback_user_data_set(uart_dev, serial_rx_cb, NULL);
    uart_irq_rx_enable(uart_dev);
#endif

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
    thingset_sdk_reschedule_work(&reporting_work, K_NO_WAIT);
#endif

    return 0;
}

SYS_INIT(thingset_serial_init, APPLICATION, THINGSET_INIT_PRIORITY_DEFAULT);

#ifndef CONFIG_UART_INTERRUPT_DRIVEN
static void thingset_serial_polling_thread()
{
    if (!device_is_ready(uart_dev)) {
        return;
    }

    while (true) {
        uint8_t c;
        while (uart_poll_in(uart_dev, &c) == 0) {
            serial_rx_buf_put(c);
        }
        k_sleep(K_MSEC(1));
    }
}

K_THREAD_DEFINE(thingset_serial_polling, 256, thingset_serial_polling_thread, NULL, NULL, NULL, 6,
                0, 1000);
#endif
