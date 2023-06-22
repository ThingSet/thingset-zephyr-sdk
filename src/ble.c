/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <thingset.h>
#include <thingset/ble.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(thingset_ble, CONFIG_LOG_DEFAULT_LEVEL);

/* ThingSet Custom Service: xxxxyyyy-5a19-4887-9c6a-14ad27bfc06d */
#define BT_UUID_THINGSET_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x00000001, 0x5a19, 0x4887, 0x9c6a, 0x14ad27bfc06d)

#define BT_UUID_THINGSET_REQUEST_VAL \
    BT_UUID_128_ENCODE(0x00000002, 0x5a19, 0x4887, 0x9c6a, 0x14ad27bfc06d)

#define BT_UUID_THINGSET_RESPONSE_VAL \
    BT_UUID_128_ENCODE(0x00000003, 0x5a19, 0x4887, 0x9c6a, 0x14ad27bfc06d)

#define BT_UUID_THINGSET_SERVICE  BT_UUID_DECLARE_128(BT_UUID_THINGSET_SERVICE_VAL)
#define BT_UUID_THINGSET_REQUEST  BT_UUID_DECLARE_128(BT_UUID_THINGSET_REQUEST_VAL)
#define BT_UUID_THINGSET_RESPONSE BT_UUID_DECLARE_128(BT_UUID_THINGSET_RESPONSE_VAL)

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/* SLIP protocol (RFC 1055) special characters */
#define SLIP_END     (0xC0)
#define SLIP_ESC     (0xDB)
#define SLIP_ESC_END (0xDC)
#define SLIP_ESC_ESC (0xDD)

static ssize_t thingset_ble_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags);

static void thingset_ble_disconn(struct bt_conn *conn, uint8_t reason);

static void thingset_ble_conn(struct bt_conn *conn, uint8_t err);

static void thingset_ble_ccc_change(const struct bt_gatt_attr *attr, uint16_t value);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, BT_UUID_THINGSET_SERVICE_VAL),
};

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = thingset_ble_conn,
    .disconnected = thingset_ble_disconn,
};

/* UART Service Declaration, order of parameters matters! */
BT_GATT_SERVICE_DEFINE(thingset_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_THINGSET_SERVICE),
                       BT_GATT_CHARACTERISTIC(BT_UUID_THINGSET_REQUEST,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, NULL,
                                              thingset_ble_rx, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_THINGSET_RESPONSE, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, NULL, NULL, NULL),
                       BT_GATT_CCC(thingset_ble_ccc_change,
                                   BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* position of BT_GATT_CCC in array created by BT_GATT_SERVICE_DEFINE */
const struct bt_gatt_attr *attr_ccc_req = &thingset_svc.attrs[3];

static struct bt_conn *ble_conn;

volatile bool notify_resp;

static char rx_buf[CONFIG_THINGSET_BLE_RX_BUF_SIZE];

static volatile size_t rx_buf_pos = 0;
static bool discard_buffer;

/* binary semaphore used as mutex in ISR context */
static struct k_sem rx_buf_lock;

static thingset_sdk_rx_callback_t rx_callback;

static struct k_work_delayable processing_work;
static struct k_work_delayable reporting_work;

static void thingset_ble_ccc_change(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    notify_resp = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notification %s", notify_resp ? "enabled" : "disabled");
}

/*
 * Receives data from BLE interface and decodes it using RFC 1055 SLIP protocol
 */
static ssize_t thingset_ble_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    /* store across multiple packages whether we had an escape char */
    static bool escape = false;

    bool finished = true;
    if (k_sem_take(&rx_buf_lock, K_NO_WAIT) == 0) {
        for (int i = 0; i < len; i++) {
            uint8_t c = *((uint8_t *)buf + i);
            if (escape) {
                if (c == SLIP_ESC_END) {
                    c = SLIP_END;
                }
                else if (c == SLIP_ESC_ESC) {
                    c = SLIP_ESC;
                }
                /* else: protocol violation, pass character as is */
                escape = false;
            }
            else if (c == SLIP_ESC) {
                escape = true;
                continue;
            }
            else if (c == SLIP_END) {
                if (finished) {
                    /* previous run finished and SLIP_END was used as new start byte */
                    continue;
                }
                else {
                    finished = true;
                    if (discard_buffer) {
                        rx_buf_pos = 0;
                        discard_buffer = false;
                        k_sem_give(&rx_buf_lock);
                        return len;
                    }
                    else {
                        rx_buf[rx_buf_pos] = '\0';
                        /* start processing the request and keep the rx_buf_lock */
                        thingset_sdk_reschedule_work(&processing_work, K_NO_WAIT);
                        return len;
                    }
                }
            }
            else {
                finished = false;
            }
            rx_buf[rx_buf_pos++] = c;
        }
        k_sem_give(&rx_buf_lock);
    }
    else {
        /* buffer not available: drop incoming data */
        LOG_HEXDUMP_WRN(buf, len, "Discarded buffer");
        discard_buffer = true;
    }

    return len;
}

static void thingset_ble_conn(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (err) {
        LOG_ERR("Connection failed (err %u)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected %s", addr);

    ble_conn = bt_conn_ref(conn);
}

static void thingset_ble_disconn(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected %s (reason %u)", addr, reason);

    if (ble_conn) {
        bt_conn_unref(ble_conn);
        ble_conn = NULL;
    }
}

int thingset_ble_send(const uint8_t *buf, size_t len)
{
    if (ble_conn && notify_resp) {
        /* Max. notification: ATT_MTU - 3 */
        const uint16_t max_mtu = bt_gatt_get_mtu(ble_conn) - 3;

        /* even max. possible size of 251 bytes should be OK to allocate on stack */
        uint8_t chunk[max_mtu];
        chunk[0] = SLIP_END;

        int pos_buf = 0;
        int pos_chunk = 1;
        bool finished = false;
        while (!finished) {
            while (pos_chunk < max_mtu && pos_buf < len) {
                if (buf[pos_buf] == SLIP_END) {
                    chunk[pos_chunk++] = SLIP_ESC;
                    chunk[pos_chunk++] = SLIP_ESC_END;
                }
                else if (buf[pos_buf] == SLIP_ESC) {
                    chunk[pos_chunk++] = SLIP_ESC;
                    chunk[pos_chunk++] = SLIP_ESC_ESC;
                }
                else {
                    chunk[pos_chunk++] = buf[pos_buf];
                }
                pos_buf++;
            }
            if (pos_chunk < max_mtu - 1 && pos_buf >= len) {
                chunk[pos_chunk++] = SLIP_END;
                finished = true;
            }
            bt_gatt_notify(ble_conn, attr_ccc_req, chunk, pos_chunk);
            pos_chunk = 0;
        }

        return 0;
    }
    else {
        return -EIO;
    }
}

int thingset_ble_send_report(const char *path)
{
    struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
    k_sem_take(&tx_buf->lock, K_FOREVER);

    int len =
        thingset_report_path(&ts, tx_buf->data, tx_buf->size, path, THINGSET_TXT_NAMES_VALUES);
    int ret = thingset_ble_send(tx_buf->data, len);

    k_sem_give(&tx_buf->lock);
    return ret;
}

static void ble_regular_report_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    static int64_t pub_time;

    if (pub_live_data_enable) {
        thingset_ble_send_report(SUBSET_LIVE_PATH);
    }

    pub_time += 1000 * pub_live_data_period;
    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(pub_time));
}

static void ble_process_msg_handler(struct k_work *work)
{
    if (rx_buf_pos > 0) {
        LOG_DBG("Received Request (%d bytes): %s", rx_buf_pos, rx_buf);

        if (rx_callback == NULL) {
            struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
            k_sem_take(&tx_buf->lock, K_FOREVER);

            int len = thingset_process_message(&ts, (uint8_t *)rx_buf, rx_buf_pos, tx_buf->data,
                                               tx_buf->size);

            thingset_ble_send(tx_buf->data, len);
            k_sem_give(&tx_buf->lock);
        }
        else {
            /* external processing (e.g. for gateway applications) */
            rx_callback(rx_buf, rx_buf_pos);
        }
    }

    // release buffer and start waiting for new commands
    rx_buf_pos = 0;
    k_sem_give(&rx_buf_lock);
}

void thingset_ble_set_rx_callback(thingset_sdk_rx_callback_t rx_cb)
{
    rx_callback = rx_cb;
}

static int thingset_ble_init()
{
    k_sem_init(&rx_buf_lock, 1, 1);

    k_work_init_delayable(&processing_work, ble_process_msg_handler);
    k_work_init_delayable(&reporting_work, ble_regular_report_handler);

    int err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return err;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return err;
    }
    LOG_INF("Waiting for Bluetooth connections...");

    thingset_sdk_reschedule_work(&reporting_work, K_NO_WAIT);

    return 0;
}

SYS_INIT(thingset_ble_init, APPLICATION, THINGSET_INIT_PRIORITY_DEFAULT);
