/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/lorawan/lorawan.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

LOG_MODULE_REGISTER(thingset_lorawan);

#define LORA_RADIO_NODE DT_NODELABEL(lora)
BUILD_ASSERT(DT_NODE_HAS_STATUS(LORA_RADIO_NODE, okay), "No LoRa radio specified in DT");

static uint8_t tx_buf[51];

char lorawan_join_eui[8 * 2 + 1] = "0000000000000000";
char lorawan_app_key[16 * 2 + 1] = "";
uint32_t lorawan_dev_nonce;
#ifdef CONFIG_THINGSET_LORAWAN_ABP
char lorawan_dev_addr[4 * 2 + 1] = "";
char lorawan_app_skey[16 * 2 + 1] = "";
char lorawan_nwk_skey[16 * 2 + 1] = "";
bool abp = false;
#endif

THINGSET_ADD_GROUP(TS_ID_ROOT, TS_ID_LORAWAN, "LoRaWAN", THINGSET_NO_CALLBACK);

THINGSET_ADD_ITEM_STRING(TS_ID_LORAWAN, TS_ID_LORAWAN_DEV_EUI, "cDevEUI", node_id, sizeof(node_id),
                         THINGSET_ANY_R, 0);

THINGSET_ADD_ITEM_STRING(TS_ID_LORAWAN, TS_ID_LORAWAN_JOIN_EUI, "pJoinEUI", lorawan_join_eui,
                         sizeof(lorawan_join_eui), THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_LORAWAN, TS_ID_LORAWAN_APP_KEY, "pAppKey", lorawan_app_key,
                         sizeof(lorawan_app_key), THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_UINT32(TS_ID_LORAWAN, TS_ID_LORAWAN_DEV_NONCE, "pDevNonce", &lorawan_dev_nonce,
                         THINGSET_ANY_RW, TS_SUBSET_NVM);

#ifdef CONFIG_THINGSET_LORAWAN_ABP
THINGSET_ADD_ITEM_BOOL(TS_ID_LORAWAN, TS_ID_LORAWAN_ABP, "pAbp", &abp, THINGSET_ANY_RW,
                       TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_LORAWAN, TS_ID_LORAWAN_DEV_ADDR, "pDevAddr", lorawan_dev_addr,
                         sizeof(lorawan_dev_addr), THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_LORAWAN, TS_ID_LORAWAN_APP_SKEY, "pAppSKey", lorawan_app_skey,
                         sizeof(lorawan_app_skey), THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_LORAWAN, TS_ID_LORAWAN_NWK_SKEY, "pNwkSKey", lorawan_nwk_skey,
                         sizeof(lorawan_nwk_skey), THINGSET_ANY_RW, TS_SUBSET_NVM);
#endif

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

#ifdef CONFIG_THINGSET_LORAWAN_ABP
    if (abp) {
        uint8_t app_skey[16];
        uint8_t nwk_skey[16];
        uint32_t dev_addr = 0;

        hex2bin(lorawan_app_skey, strlen(lorawan_app_skey), app_skey, sizeof(app_skey));
        hex2bin(lorawan_nwk_skey, strlen(lorawan_nwk_skey), nwk_skey, sizeof(nwk_skey));
        hex2bin(lorawan_dev_addr, strlen(lorawan_dev_addr), (uint8_t *)&dev_addr, sizeof(dev_addr));

        join_cfg.mode = LORAWAN_ACT_ABP;
        join_cfg.abp.dev_addr = sys_cpu_to_be32(dev_addr);
        join_cfg.abp.nwk_skey = nwk_skey;
        join_cfg.abp.app_skey = app_skey;
    }
#endif

    bool connected = false;
    uint32_t rejoin_wait_sec = 8;
    bool increased_dev_nonce = false;
    while (true) {
        if (!connected) {
            LOG_INF("Joining network over %s", join_cfg.mode ? "ABP" : "OTAA");
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
            thingset_storage_save_queued(true);

            connected = true;
        }

        int len = thingset_export_subsets(&ts, tx_buf, sizeof(tx_buf), TS_SUBSET_SUMMARY,
                                          THINGSET_BIN_IDS_VALUES);

        /* use port 0x80 + data object ID for ID/value map */
        ret = lorawan_send(0x80 + TS_ID_SUBSET_SUMMARY, tx_buf, len, LORAWAN_MSG_UNCONFIRMED);
        if (ret < 0) {
            LOG_ERR("Sending message failed: %d", ret);
        }
        else {
            LOG_HEXDUMP_INF(tx_buf, len, "Message sent: ");
        }

        k_sleep(K_MSEC(summary_reporting_period * 1000));
    }
}

K_THREAD_DEFINE(thingset_lorawan, 2048, lorawan_thread, NULL, NULL, NULL, 1, 0, 1000);
