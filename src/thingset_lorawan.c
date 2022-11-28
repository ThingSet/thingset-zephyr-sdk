/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/random/rand32.h>
#include <zephyr/sys/util.h>

#include "thingset/sdk.h"
#include "thingset/storage.h"

LOG_MODULE_REGISTER(thingset_lorawan);

#define LORA_RADIO_NODE DT_NODELABEL(lora)
BUILD_ASSERT(DT_NODE_HAS_STATUS(LORA_RADIO_NODE, okay), "No LoRa radio specified in DT");

static uint8_t tx_buf[51];

extern char node_id[17];
extern uint8_t eui64[8];

char lorawan_join_eui[8 * 2 + 1] = "0000000000000000";
char lorawan_app_key[16 * 2 + 1] = "";
uint32_t lorawan_dev_nonce;

static struct ts_data_object *report_subset;

TS_ADD_GROUP(ID_LORAWAN, "LoRaWAN", TS_NO_CALLBACK, ID_ROOT);

TS_ADD_ITEM_STRING(0x70, "cDevEUI", node_id, sizeof(node_id), ID_LORAWAN, TS_ANY_RW, 0);

TS_ADD_ITEM_STRING(0x71, "sJoinEUI", lorawan_join_eui, sizeof(lorawan_join_eui), ID_LORAWAN,
                   TS_ANY_RW, SUBSET_NVM);

TS_ADD_ITEM_STRING(0x72, "sAppKey", lorawan_app_key, sizeof(lorawan_app_key), ID_LORAWAN, TS_ANY_RW,
                   SUBSET_NVM);

TS_ADD_ITEM_UINT32(0x73, "sDevNonce", &lorawan_dev_nonce, ID_LORAWAN, TS_ANY_RW, SUBSET_NVM);

static void downlink_callback(uint8_t port, bool data_pending, int16_t rssi, int8_t snr,
                              uint8_t len, const uint8_t *data)
{
    LOG_INF("Port %d, Pending %d, RSSI %ddB, SNR %ddBm", port, data_pending, rssi, snr);
    if (data) {
        LOG_HEXDUMP_INF(data, len, "Payload: ");
    }
}

static void datarate_changed(enum lorawan_datarate dr)
{
    uint8_t unused, max_size;

    lorawan_get_payload_sizes(&unused, &max_size);
    LOG_INF("New Datarate: DR_%d, Max Payload %d", dr, max_size);
}

void lorawan_thread(void)
{
    const struct device *lora_dev;
    struct lorawan_join_config join_cfg;
    uint8_t join_eui[8];
    uint8_t app_key[16];
    int ret;

    hex2bin(lorawan_join_eui, strlen(lorawan_join_eui), join_eui, sizeof(join_eui));
    hex2bin(lorawan_app_key, strlen(lorawan_app_key), app_key, sizeof(app_key));

    struct lorawan_downlink_cb downlink_cb = { .port = LW_RECV_PORT_ANY, .cb = downlink_callback };
    lorawan_register_downlink_callback(&downlink_cb);
    lorawan_register_dr_changed_callback(datarate_changed);

    lora_dev = DEVICE_DT_GET(LORA_RADIO_NODE);
    if (!device_is_ready(lora_dev)) {
        LOG_ERR("%s device not ready", lora_dev->name);
        return;
    }

    ret = lorawan_start();
    if (ret < 0) {
        LOG_ERR("lorawan_start failed: %d", ret);
        return;
    }

    // previous dev_nonce read from EEPROM must be increased for new join
    lorawan_dev_nonce++;

    join_cfg.mode = LORAWAN_ACT_OTAA;
    join_cfg.dev_eui = eui64;
    join_cfg.otaa.join_eui = join_eui;
    join_cfg.otaa.app_key = app_key;
    join_cfg.otaa.nwk_key = app_key;
    join_cfg.otaa.dev_nonce = lorawan_dev_nonce;

    report_subset = ts_get_object_by_path(&ts, SUBSET_REPORT_PATH, sizeof(SUBSET_REPORT_PATH) - 1);

    bool connected = false;
    uint32_t rejoin_wait_sec = 8;
    bool increased_dev_nonce = false;
    while (true) {
        if (!connected) {
            LOG_INF("Joining network over OTAA");
            ret = lorawan_join(&join_cfg);
            if (ret < 0) {
                LOG_ERR("lorawan_join_network failed: %d", ret);

                if (ret == -ETIMEDOUT && !increased_dev_nonce) {
                    // maybe the previous DevNonce was not properly stored in EEPROM
                    LOG_INF("Increasing DevNonce for next join");
                    lorawan_dev_nonce++;
                    join_cfg.otaa.dev_nonce = lorawan_dev_nonce;
                    increased_dev_nonce = true;
                }

                uint32_t jitter = sys_rand32_get() & 0xFFF; // approx. +- 2 seconds

                // backoff to avoid too many reconnect events if no gateway is in reach
                rejoin_wait_sec = rejoin_wait_sec * 2;
                if (rejoin_wait_sec > 3600U) {
                    rejoin_wait_sec = 3600U; // wait maximum 1 hour
                }

                LOG_INF("Waiting approx. %d seconds before reconnecting", rejoin_wait_sec);

                k_sleep(K_MSEC(rejoin_wait_sec * 1000 + jitter));
                continue;
            }

            // store used DevNonce for next join
            thingset_storage_save();

            connected = true;
        }

        int len = ts_bin_export(&ts, tx_buf, sizeof(tx_buf), SUBSET_REPORT);

        ret = lorawan_send(report_subset->id, tx_buf, len, LORAWAN_MSG_UNCONFIRMED);
        if (ret < 0) {
            LOG_ERR("Sending message failed: %d", ret);
        }
        else {
            LOG_HEXDUMP_INF(tx_buf, len, "Message sent: ");
        }

        k_sleep(K_MSEC(pub_reports_period * 1000));
    }
}

K_THREAD_DEFINE(thingset_lorawan, 2048, lorawan_thread, NULL, NULL, NULL, 1, 0, 1000);
