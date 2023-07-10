/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/wifi_mgmt.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

#include <stdio.h>

LOG_MODULE_REGISTER(thingset_wifi, CONFIG_THINGSET_SDK_LOG_LEVEL);

char wifi_ssid[32] = "";
char wifi_psk[32] = "";
char ipv4_addr[16] = "";

static struct wifi_connect_req_params wifi_params;
static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_if *iface;

static struct k_work_delayable wifi_connect_work;

THINGSET_ADD_ITEM_STRING(TS_ID_NET, TS_ID_NET_WIFI_SSID, "sWiFiSSID", wifi_ssid, sizeof(wifi_ssid),
                         THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_NET, TS_ID_NET_WIFI_PSK, "sWiFiPSK", wifi_psk, sizeof(wifi_psk),
                         THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_NET, TS_ID_NET_IPV4, "rIPV4", ipv4_addr, sizeof(ipv4_addr),
                         THINGSET_ANY_RW, TS_SUBSET_NVM);

static void wifi_connect_handler(struct k_work *work)
{
    int err;

    if (strlen(wifi_ssid) == 0) {
        LOG_ERR("No SSID configured");
        return;
    }

    LOG_INF("Connecting to WiFi with SSID %s", wifi_ssid);

    wifi_params.ssid = wifi_ssid;
    wifi_params.ssid_length = strlen(wifi_params.ssid);
    wifi_params.security = WIFI_SECURITY_TYPE_PSK;
    wifi_params.psk = wifi_psk;
    wifi_params.psk_length = strlen(wifi_params.psk);
    wifi_params.channel = WIFI_CHANNEL_ANY;
    wifi_params.mfp = WIFI_MFP_OPTIONAL;

    err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &wifi_params,
                   sizeof(struct wifi_connect_req_params));
    if (err) {
        LOG_ERR("WiFi connection request failed");
        return;
    }
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event,
                                    struct net_if *iface)
{
    const struct wifi_status *status = cb->info;
    int err;

    switch (mgmt_event) {
        case NET_EVENT_WIFI_CONNECT_RESULT: {
            struct net_if_ipv4 *ipv4;
            err = net_if_config_ipv4_get(iface, &ipv4);
            if (!err) {
                uint32_t ip = ipv4->unicast[0].address.in_addr.s_addr;
                snprintf(ipv4_addr, sizeof(ipv4_addr), "%d.%d.%d.%d", ip & 0xFF, (ip >> 8) & 0xFF,
                         (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
                LOG_INF("WiFi connected with status %d, IP: %s", status->status, ipv4_addr);
            }
            break;
        }
        case NET_EVENT_WIFI_DISCONNECT_RESULT:
            ipv4_addr[0] = '\0';
            LOG_INF("WiFi disconnected, trying to reconnect in 60s");
            thingset_sdk_reschedule_work(&wifi_connect_work, K_SECONDS(60));
            break;
        default:
            break;
    }
}

static int wifi_init(void)
{
    k_work_init_delayable(&wifi_connect_work, wifi_connect_handler);

    iface = net_if_get_default();
    if (!iface) {
        LOG_ERR("WiFi interface not available");
        return -ENODEV;
    }

    net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler,
                                 NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_mgmt_cb);

    /* attempt to connect after a short delay */
    thingset_sdk_reschedule_work(&wifi_connect_work, K_SECONDS(3));

    return 0;
}

SYS_INIT(wifi_init, APPLICATION, THINGSET_INIT_PRIORITY_DEFAULT);
