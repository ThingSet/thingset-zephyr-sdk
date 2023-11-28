/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/websocket.h>
#include <zephyr/random/random.h>
#include <zephyr/shell/shell.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/serial.h>
#include <thingset/websocket.h>

#include <signal.h>
#include <stdio.h>

LOG_MODULE_REGISTER(thingset_websocket, CONFIG_THINGSET_SDK_LOG_LEVEL);

#define CA_CERTIFICATE_TAG 1

static const unsigned char ca_certificate[] = {
#include "certs/isrgrootx1.der.inc"
};

static uint8_t rx_buf[CONFIG_THINGSET_WEBSOCKET_RX_BUF_SIZE];

static char server_host[64] = CONFIG_THINGSET_WEBSOCKET_SERVER_HOST;
static uint16_t server_port = CONFIG_THINGSET_WEBSOCKET_SERVER_PORT;
static bool use_tls = IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS);
static char auth_token[32] = CONFIG_THINGSET_WEBSOCKET_AUTH_TOKEN;
static char server_path[23]; /* "/node/" + pNodeID (16 bytes) + '\0' */

static int websock = -1;

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
static struct k_work_delayable reporting_work;
#endif

THINGSET_ADD_ITEM_STRING(TS_ID_NET, TS_ID_NET_WEBSOCKET_HOST, "sWebsocketHost", server_host,
                         sizeof(server_host), THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_UINT16(TS_ID_NET, TS_ID_NET_WEBSOCKET_PORT, "sWebsocketPort", &server_port,
                         THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_BOOL(TS_ID_NET, TS_ID_NET_WEBSOCKET_USE_TLS, "sWebsocketTLS", &use_tls,
                       THINGSET_ANY_RW, TS_SUBSET_NVM);

THINGSET_ADD_ITEM_STRING(TS_ID_NET, TS_ID_NET_WEBSOCKET_AUTH_TOKEN, "sWebsocketAuthToken",
                         auth_token, sizeof(auth_token), THINGSET_ANY_RW, TS_SUBSET_NVM);

static int connect_server(int *sock, sa_family_t family, const char *host, uint16_t port)
{
    const char *family_str = family == AF_INET ? "IPv4" : "IPv6";
    static struct addrinfo hints;
    struct addrinfo *addr;
    int ret = 0;
    char port_str[6];

    sprintf(port_str, "%u", port);

    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    ret = getaddrinfo(host, port_str, &hints, &addr);
    if (ret != 0) {
        LOG_ERR("Unable to resolve %s for %s, ret:%d, errno:%d", family_str, host, ret, errno);
        return ret;
    }
    else {
        struct sockaddr_in *sa_in = (struct sockaddr_in *)addr->ai_addr;
        char addr_str[INET_ADDRSTRLEN];
        zsock_inet_ntop(AF_INET, &sa_in->sin_addr, addr_str, sizeof(addr_str));
        LOG_INF("Resolved %s: %s", family_str, addr_str);
    }

    if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS) && use_tls) {
        sec_tag_t sec_tag_list[] = {
            CA_CERTIFICATE_TAG,
        };

        *sock = socket(addr->ai_family, addr->ai_socktype, IPPROTO_TLS_1_2);
        if (*sock >= 0) {
            ret = setsockopt(*sock, SOL_TLS, TLS_SEC_TAG_LIST, sec_tag_list, sizeof(sec_tag_list));
            if (ret < 0) {
                LOG_ERR("Failed to set secure option (%d)", -errno);
                ret = -errno;
                goto fail;
            }

            ret = setsockopt(*sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host) + 1);
            if (ret < 0) {
                LOG_ERR("Failed to set TLS_HOSTNAME option (%d)", -errno);
                ret = -errno;
                goto fail;
            }
        }
    }
    else {
        *sock = socket(addr->ai_family, addr->ai_socktype, IPPROTO_TCP);
    }

    if (*sock < 0) {
        LOG_ERR("Failed to create TCP socket (%d)", -errno);
        return -errno;
    }

    ret = connect(*sock, addr->ai_addr, addr->ai_addrlen);
    if (ret < 0) {
        LOG_ERR("Failed to connect to socket (%d)", -errno);
        ret = -errno;
        goto fail;
    }

    return 0;

fail:
    if (*sock >= 0) {
        close(*sock);
        *sock = -1;
    }

    return ret;
}

static int connect_cb(int sock, struct http_request *req, void *user_data)
{
    LOG_INF("Websocket %d connected.", sock);

    return 0;
}

static int recv_data(int sock, uint8_t *buf, size_t buf_len)
{
    uint64_t remaining = ULLONG_MAX;
    int total_read;
    uint32_t message_type;
    int ret, read_pos;

    read_pos = 0;
    total_read = 0;

    while (remaining > 0) {
        ret = websocket_recv_msg(sock, buf + read_pos, buf_len - read_pos, &message_type,
                                 &remaining, 0);
        if (ret < 0) {
            if (ret == -EAGAIN) {
                k_sleep(K_MSEC(50));
                continue;
            }

            LOG_DBG("Socket connection closed while waiting (%d/%d)", ret, errno);
            return -1;
        }
        LOG_DBG("Read %d bytes from socket", ret);

        read_pos += ret;
        total_read += ret;
    }

    if (remaining != 0) {
        LOG_ERR("Data recv failure after %d bytes (remaining %" PRId64 ")", total_read, remaining);
        LOG_HEXDUMP_DBG(buf, total_read, "received ws buf");
        return -1;
    }
    else {
        LOG_DBG("Received %d bytes in total", total_read);
        return total_read;
    }
}

int thingset_websocket_send(const uint8_t *buf, size_t len)
{
    if (websock < 0) {
        return -EIO;
    }

    int bytes_sent = websocket_send_msg(websock, buf, len, WEBSOCKET_OPCODE_DATA_TEXT, true, true,
                                        SYS_FOREVER_MS);

    if (bytes_sent < 0) {
        LOG_ERR("Failed to send data via WebSocket: %d", bytes_sent);
    }

    return 0;
}

int thingset_websocket_send_report(const char *path)
{
    struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
    k_sem_take(&tx_buf->lock, K_FOREVER);

    int len =
        thingset_report_path(&ts, tx_buf->data, tx_buf->size, path, THINGSET_TXT_NAMES_VALUES);

    int ret = thingset_websocket_send(tx_buf->data, len);

    k_sem_give(&tx_buf->lock);
    return ret;
}

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS

static void websocket_regular_report_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    static int64_t pub_time;

    if (live_reporting_enable && websock >= 0) {
        thingset_websocket_send_report(TS_NAME_SUBSET_LIVE);
    }

    pub_time += 1000 * live_reporting_period;
    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(pub_time));
}

#endif

#ifdef CONFIG_BOARD_NATIVE_POSIX
static struct sigaction sigact_default;

static void websocket_shutdown(int sig)
{
    if (websock >= 0) {
        LOG_INF("Closing websocket %d", websock);
        /* closing the websocket will also close the underlying socket */
        websocket_disconnect(websock);
    }

    /* also call default handler */
    (*sigact_default.sa_handler)(sig);
}
#endif

static void websocket_thread(void)
{
    char auth_header[64];
    int32_t timeout = 3 * MSEC_PER_SEC;
    int sock = -1;
    int ret;

#ifdef CONFIG_BOARD_NATIVE_POSIX
    /* Ensure graceful shutdown of the socket for Ctrl+C on the console. */
    struct sigaction sigact = { 0 };
    sigact.sa_handler = websocket_shutdown;
    sigemptyset(&sigact.sa_mask);
    sigaction(SIGINT, &sigact, &sigact_default);
#endif

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
    k_work_init_delayable(&reporting_work, websocket_regular_report_handler);
    thingset_sdk_reschedule_work(&reporting_work, K_NO_WAIT);
#endif

    if (IS_ENABLED(CONFIG_NET_SOCKETS_SOCKOPT_TLS)) {
        ret = tls_credential_add(CA_CERTIFICATE_TAG, TLS_CREDENTIAL_CA_CERTIFICATE, ca_certificate,
                                 sizeof(ca_certificate));
        if (ret < 0) {
            LOG_ERR("Failed to register public certificate: %d", ret);
            return;
        }
    }

    snprintf(server_path, sizeof(server_path), "/node/%s", node_id);

    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", auth_token);

    const char *extra_headers[] = { auth_header, NULL };

    LOG_INF("Establishing WebSocket connection to %s:%d", server_host, server_port);

    while (true) {

        ret = connect_server(&sock, AF_INET, server_host, server_port);
        if (ret < 0 || sock < 0) {
            k_sleep(K_SECONDS(10));
            continue;
        }

        struct websocket_request req = { 0 };
        req.host = server_host;
        req.url = server_path;
        req.optional_headers = extra_headers;
        req.cb = connect_cb;
        /* tmp_buf only used for connecting, so we can re-use our rx buffer */
        req.tmp_buf = rx_buf;
        req.tmp_buf_len = sizeof(rx_buf);

        websock = websocket_connect(sock, &req, timeout, NULL);
        if (websock >= 0) {
            LOG_INF("WebSocket connection established.");
        }
        else {
            LOG_ERR("Failed to connect to WebSocket (%d)", websock);
            close(sock);
            k_sleep(K_SECONDS(10));
            continue;
        }

        while (websock >= 0) {
            int bytes_received = recv_data(websock, rx_buf, sizeof(rx_buf));
            if (bytes_received < 0) {
                websocket_disconnect(websock);
                break;
            }

            struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
            k_sem_take(&tx_buf->lock, K_FOREVER);

            int len = thingset_process_message(&ts, (uint8_t *)rx_buf, bytes_received, tx_buf->data,
                                               tx_buf->size);
            if (len > 0) {
                LOG_DBG("Sending response with %d bytes", len);
                thingset_websocket_send(tx_buf->data, len);
            }

            k_sem_give(&tx_buf->lock);
        }
    }
}

K_THREAD_DEFINE(thingset_websocket, CONFIG_THINGSET_WEBSOCKET_THREAD_STACK_SIZE, websocket_thread,
                NULL, NULL, NULL, CONFIG_THINGSET_WEBSOCKET_THREAD_PRIORITY, 0, 0);
