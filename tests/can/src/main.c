/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/ztest.h>

#include <thingset.h>
#include <thingset/can.h>
#include <thingset/sdk.h>

#define TEST_RECEIVE_TIMEOUT K_MSEC(100)

static const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

static struct k_sem request_tx_sem;
static struct k_sem response_rx_sem;
uint8_t *response;
size_t response_len;
int response_code;

static struct k_sem item_rx_sem;
static uint16_t item_data_id;
static uint8_t item_value_buf[8];
static size_t item_value_len;

static struct k_sem report_rx_sem;
static uint8_t report_buf[100];
static size_t report_len;

/* test data objects */
static float test_float = 1234.56;
static char test_string[] = "Hello World!";

THINGSET_ADD_GROUP(THINGSET_ID_ROOT, 0x200, "Test", THINGSET_NO_CALLBACK);
THINGSET_ADD_ITEM_FLOAT(0x200, 0x201, "wFloat", &test_float, 1, THINGSET_ANY_RW, TS_SUBSET_LIVE);
THINGSET_ADD_ITEM_STRING(0x200, 0x202, "wString", test_string, sizeof(test_string), THINGSET_ANY_RW,
                         TS_SUBSET_LIVE);

static void isotp_fast_recv_cb(struct net_buf *buffer, int rem_len, uint32_t rx_can_id, void *arg)
{
    response = malloc(buffer->len);
    memcpy(response, buffer->data, buffer->len);
    response_code = 0;
    response_len = buffer->len;
    k_sem_give(&response_rx_sem);
}

static void isotp_fast_sent_cb(int result, void *arg)
{
    response_code = result;
    k_sem_give(&request_tx_sem);
}

static void item_rx_callback(uint16_t data_id, const uint8_t *value, size_t value_len,
                             uint8_t source_addr)
{
    if (value_len < sizeof(item_value_buf)) {
        item_data_id = data_id;
        memcpy(item_value_buf, value, value_len);
        item_value_len = value_len;
        k_sem_give(&item_rx_sem);
    }
}

static void report_rx_callback(const uint8_t *buf, size_t len, uint8_t source_addr)
{
    if (len < sizeof(report_buf)) {
        memcpy(report_buf, buf, len);
        report_len = len;
        k_sem_give(&report_rx_sem);
    }
}

ZTEST(thingset_can, test_receive_item_from_node)
{
    struct can_frame rx_frame = {
        .id = 0x1E123402, /* node with address 0x02 */
        .flags = CAN_FRAME_IDE,
        .data = { 0xF6 },
        .dlc = 1,
    };
    int err;

    k_sem_reset(&item_rx_sem);

    err = can_send(can_dev, &rx_frame, K_MSEC(10), NULL, NULL);
    zassert_equal(err, 0, "can_send failed: %d", err);

    err = k_sem_take(&item_rx_sem, TEST_RECEIVE_TIMEOUT);
    zassert_equal(err, 0, "receive timeout");
    zassert_equal(item_data_id, 0x1234, "wrong data object ID");
    zassert_equal(item_value_len, 1, "wrong value len");
    zassert_equal(item_value_buf[0], 0xF6);
}

ZTEST(thingset_can, test_receive_packetized_report)
{
    /* "hello world" */
    uint8_t report_exp[] = { 0x1F, 0x19, 0x12, 0x34, 0x6B, 0x68, 0x65, 0x6C,
                             0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64 };

    struct can_frame report_frames[] = {
        {
            .id = 0x1D000002, /* node with address 0x02, seq 0x0, msg 0x0 */
            .flags = CAN_FRAME_IDE,
            .data = { 0x1F, 0x19, 0x12, 0x34, 0x6B, 0x68, 0x65, 0x6C },
            .dlc = 8,
        },
        {
            .id = 0x1D001102, /* node with address 0x02, seq 0x1, msg 0x0, end = true */
            .flags = CAN_FRAME_IDE,
            .data = { 0x6C, 0x6F, 0x20, 0x77, 0x6F, 0x72, 0x6C, 0x64 },
            .dlc = 8,
        },
    };
    int err;

    k_sem_reset(&report_rx_sem);

    for (int i = 0; i < ARRAY_SIZE(report_frames); i++) {
        err = can_send(can_dev, &report_frames[i], K_MSEC(10), NULL, NULL);
        zassert_equal(err, 0, "can_send failed: %d", err);
    }

    err = k_sem_take(&report_rx_sem, TEST_RECEIVE_TIMEOUT);
    zassert_equal(err, 0, "receive timeout");
    zassert_equal(report_len, sizeof(report_exp), "wrong report len %d (expected %d)", report_len,
                  sizeof(report_exp));
    zassert_mem_equal(report_buf, report_exp, sizeof(report_exp));
}

CAN_MSGQ_DEFINE(report_packets_msgq, 10);

ZTEST(thingset_can, test_send_packetized_report)
{
    char report_exp[] = "#Test {\"wFloat\":1234.6,\"wString\":\"Hello World!\"}";
    struct can_filter rx_filter = {
        .id = 0x1D000000,
        .mask = 0x1F000000,
        .flags = CAN_FILTER_DATA | CAN_FILTER_IDE,
    };
    struct can_frame rx_frame;
    int err;

    k_msgq_purge(&report_packets_msgq);

    int filter_id = can_add_rx_filter_msgq(can_dev, &report_packets_msgq, &rx_filter);
    zassert_false(filter_id < 0, "adding rx filter failed: %d", filter_id);

    struct thingset_can *ctx = thingset_can_get_inst();
    ctx->msg_no = 0;

    for (uint32_t msg_no = 0; msg_no < 10; msg_no++) {
        err = thingset_can_send_report("Test", THINGSET_TXT_NAMES_VALUES);
        zassert_equal(err, 0, "sending report failed: %d", err);

        for (uint32_t seq = 0; seq * CAN_MAX_DLEN < strlen(report_exp); seq++) {
            int missing_len = strlen(report_exp) - seq * CAN_MAX_DLEN;
            uint32_t end = missing_len <= CAN_MAX_DLEN ? 0x00001000 : 0;
            err = k_msgq_get(&report_packets_msgq, &rx_frame, K_MSEC(100));
            zassert_equal(err, 0, "receiving CAN frame %d timed out", seq);
            zassert_equal(rx_frame.id, 0x1d000001 | ((msg_no & 0x7) << 13) | end | (seq << 8),
                          "CAN ID 0x%x for seq %d of msg %d not correct", rx_frame.id, seq, msg_no);
            zassert_mem_equal(rx_frame.data, report_exp + seq * CAN_MAX_DLEN,
                              end ? missing_len : CAN_MAX_DLEN);
        }
    }

    can_remove_rx_filter(can_dev, filter_id);
}

static void request_rx_cb(const struct device *dev, struct can_frame *frame, void *user_data)
{
    k_sem_give(&request_tx_sem);
}

ZTEST(thingset_can, test_send_request_to_node)
{
    struct can_filter other_node_filter = {
        .id = 0x1800CC00,
        .mask = 0x1F00FF00,
        .flags = CAN_FILTER_DATA | CAN_FILTER_IDE,
    };
    uint8_t req_buf[] = { 0x01, 0x00 }; /* simple single-frame request via ISO-TP */
    int err;

    k_sem_reset(&request_tx_sem);

    int filter_id = can_add_rx_filter(can_dev, &request_rx_cb, NULL, &other_node_filter);
    zassert_false(filter_id < 0, "adding rx filter failed: %d", filter_id);

#ifdef CONFIG_ISOTP_FAST
    thingset_can_send(req_buf, sizeof(req_buf), 0xCC, 0x0, NULL, NULL, TEST_RECEIVE_TIMEOUT);
#else
    thingset_can_send(req_buf, sizeof(req_buf), 0xCC, 0x0);
#endif

    err = k_sem_take(&request_tx_sem, TEST_RECEIVE_TIMEOUT);
    zassert_equal(err, 0, "receive timeout");

    can_remove_rx_filter(can_dev, filter_id);
}

ZTEST(thingset_can, test_request_response)
{
    k_sem_reset(&request_tx_sem);
    k_sem_reset(&response_rx_sem);

    struct isotp_fast_ctx client_ctx;
    struct isotp_fast_opts opts = {
        .bs = 0,
        .stmin = 0,
        .flags = 0,
    };
    int err = isotp_fast_bind(&client_ctx, can_dev, 0x1800cc00, &opts, isotp_fast_recv_cb, NULL,
                              NULL, isotp_fast_sent_cb);
    zassert_equal(err, 0, "bind fail");

    /* GET CAN node address */
    uint8_t msg[] = { 0x01, 0x19, TS_ID_NET_CAN_NODE_ADDR >> 8, TS_ID_NET_CAN_NODE_ADDR & 0xFF };
    err = isotp_fast_send(&client_ctx, msg, sizeof(msg), 0x01, 0x0, NULL);
    zassert_equal(err, 0, "send fail");
    k_sem_take(&request_tx_sem, TEST_RECEIVE_TIMEOUT);

    k_sem_take(&response_rx_sem, TEST_RECEIVE_TIMEOUT);
    zassert_equal(response_code, 0, "receive fail");

    /* expected response is 0x01 for CAN node address */
    uint8_t resp_exp[] = { 0x85, 0xF6, 0x01 };
    zassert_equal(response_len, 3, "unexpected response length %d", response_len);
    zassert_mem_equal(response, resp_exp, sizeof(resp_exp), "unexpected response");
    free(response);
    isotp_fast_unbind(&client_ctx);
}

static void *thingset_can_setup(void)
{
    int err;

    k_sem_init(&item_rx_sem, 0, 1);
    k_sem_init(&report_rx_sem, 0, 1);
    k_sem_init(&request_tx_sem, 0, 1);
    k_sem_init(&response_rx_sem, 0, 1);

    thingset_init_global(&ts);

    zassert_true(device_is_ready(can_dev), "CAN device not ready");

    (void)can_stop(can_dev);

    err = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
    zassert_equal(err, 0, "failed to set loopback mode (err %d)", err);

    err = can_start(can_dev);
    zassert_equal(err, 0, "failed to start CAN controller (err %d)", err);

    /* wait for address claiming to finish */
    k_sleep(K_MSEC(1000));

    thingset_can_set_item_rx_callback(item_rx_callback);
    thingset_can_set_report_rx_callback(report_rx_callback);

    return NULL;
}

ZTEST_SUITE(thingset_can, NULL, thingset_can_setup, NULL, NULL, NULL);
