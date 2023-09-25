/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/rand32.h>

#include <thingset.h>
#include <thingset/can.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

LOG_MODULE_REGISTER(thingset_can, CONFIG_THINGSET_SDK_LOG_LEVEL);

extern uint8_t eui64[8];

#define EVENT_ADDRESS_CLAIM_MSG_SENT    0x01
#define EVENT_ADDRESS_CLAIMING_FINISHED 0x02
#define EVENT_ADDRESS_ALREADY_USED      0x03

static const struct can_filter report_filter = {
    .id = THINGSET_CAN_TYPE_REPORT,
    .mask = THINGSET_CAN_TYPE_MASK,
    .flags = CAN_FILTER_DATA | CAN_FILTER_IDE,
};

static const struct can_filter addr_claim_filter = {
    .id = THINGSET_CAN_TYPE_NETWORK | THINGSET_CAN_TARGET_SET(THINGSET_CAN_ADDR_BROADCAST),
    .mask = THINGSET_CAN_TYPE_MASK | THINGSET_CAN_TARGET_MASK,
    .flags = CAN_FILTER_DATA | CAN_FILTER_IDE,
};

static const struct isotp_fc_opts fc_opts = {
    .bs = 8,    /* block size */
    .stmin = 1, /* minimum separation time = 100 ms */
};

static void thingset_can_addr_claim_tx_cb(const struct device *dev, int error, void *user_data)
{
    struct thingset_can *ts_can = user_data;

    if (error == 0) {
        k_event_post(&ts_can->events, EVENT_ADDRESS_CLAIM_MSG_SENT);
    }
    else {
        LOG_ERR("Address claim failed with %d", error);
    }
}

static void thingset_can_addr_claim_tx_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct thingset_can *ts_can = CONTAINER_OF(dwork, struct thingset_can, addr_claim_work);

    struct can_frame tx_frame = {
        .flags = CAN_FRAME_IDE,
    };
    tx_frame.id = THINGSET_CAN_TYPE_NETWORK | THINGSET_CAN_PRIO_NETWORK_MGMT
                  | THINGSET_CAN_TARGET_SET(THINGSET_CAN_ADDR_BROADCAST)
                  | THINGSET_CAN_SOURCE_SET(ts_can->node_addr);
    tx_frame.dlc = sizeof(eui64);
    memcpy(tx_frame.data, eui64, sizeof(eui64));

    int err = can_send(ts_can->dev, &tx_frame, K_MSEC(100), thingset_can_addr_claim_tx_cb, ts_can);
    if (err != 0) {
        LOG_ERR("Address claim failed with %d", err);
    }
}

static void thingset_can_addr_discovery_rx_cb(const struct device *dev, struct can_frame *frame,
                                              void *user_data)
{
    struct thingset_can *ts_can = user_data;

    LOG_INF("Received address discovery frame with ID %X (rand %.2X)", frame->id,
            THINGSET_CAN_RAND_GET(frame->id));

    thingset_sdk_reschedule_work(&ts_can->addr_claim_work, K_NO_WAIT);
}

static void thingset_can_addr_claim_rx_cb(const struct device *dev, struct can_frame *frame,
                                          void *user_data)
{
    struct thingset_can *ts_can = user_data;
    uint8_t *data = frame->data;

    LOG_INF("Received address claim from node 0x%.2X with EUI-64 "
            "%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x",
            THINGSET_CAN_SOURCE_GET(frame->id), data[0], data[1], data[2], data[3], data[4],
            data[5], data[6], data[8]);

    if (ts_can->node_addr == THINGSET_CAN_SOURCE_GET(frame->id)) {
        k_event_post(&ts_can->events, EVENT_ADDRESS_ALREADY_USED);
    }

    /* Optimization: store in internal database to exclude from potentially available addresses */
}

static void thingset_can_report_rx_cb(const struct device *dev, struct can_frame *frame,
                                      void *user_data)
{
    const struct thingset_can *ts_can = user_data;
    uint16_t data_id = THINGSET_CAN_DATA_ID_GET(frame->id);
    uint8_t source_addr = THINGSET_CAN_SOURCE_GET(frame->id);

    ts_can->report_rx_cb(data_id, frame->data, can_dlc_to_bytes(frame->dlc), source_addr);
}

static void thingset_can_report_tx_cb(const struct device *dev, int error, void *user_data)
{
    /* Do nothing: Reports are fire and forget. */
}

static void thingset_can_report_send(struct thingset_can *ts_can, struct can_frame *frame, int data_len, const thingset_object_id_t id)
{
    if (data_len > 0) {
        frame->id = THINGSET_CAN_TYPE_REPORT | THINGSET_CAN_PRIO_REPORT_LOW
                    | THINGSET_CAN_DATA_ID_SET(id)
                    | THINGSET_CAN_SOURCE_SET(ts_can->node_addr);
        frame->dlc = data_len;
        if (can_send(ts_can->dev, frame, K_MSEC(10), thingset_can_report_tx_cb, NULL) != 0) {
            LOG_DBG("Error sending CAN frame with ID %x", frame->id);
        }
    }
}

static int thingset_object_export_and_send(struct thingset_data_object *obj, void *callback_context)
{
    struct thingset_can *ts_can = (struct thingset_can *)callback_context;
    struct can_frame frame = {
        .flags = CAN_FRAME_IDE,
    };
    int data_len = thingset_export_item(&ts, frame.data, sizeof(frame.data), obj,
                                        THINGSET_BIN_VALUES_ONLY);
    if (data_len < 0) {
        return data_len;
    }
    k_sleep(K_MSEC(1)); /* if we don't do this, at least on native so far, we seem to skip every other element */
    thingset_can_report_send(ts_can, &frame, data_len, obj->id);
}

static void thingset_can_report_tx_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct thingset_can *ts_can = CONTAINER_OF(dwork, struct thingset_can, reporting_work);

    /* once CAN FD is supported, this should be conditional on that being disabled */
    ts.elementwise_array_updates = true;
    thingset_for_object_in_subsets(&ts, TS_SUBSET_LIVE, thingset_object_export_and_send, ts_can);
    ts.elementwise_array_updates = false;

    ts_can->next_pub_time += 1000 * live_reporting_period;
    if (ts_can->next_pub_time <= k_uptime_get()) {
        /* ensure proper initialization of t_start */
        ts_can->next_pub_time = k_uptime_get() + 1000 * live_reporting_period;
    }

    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(ts_can->next_pub_time));
}

int thingset_can_receive_inst(struct thingset_can *ts_can, uint8_t *rx_buffer, size_t rx_buf_size,
                              uint8_t *source_addr, k_timeout_t timeout)
{
    int ret, rem_len, rx_len;
    struct net_buf *netbuf;

    if (!device_is_ready(ts_can->dev)) {
        return -ENODEV;
    }

    ts_can->rx_addr.ext_id = THINGSET_CAN_TYPE_CHANNEL | THINGSET_CAN_PRIO_CHANNEL
                             | THINGSET_CAN_TARGET_SET(ts_can->node_addr);
    ts_can->tx_addr.ext_id = THINGSET_CAN_TYPE_CHANNEL | THINGSET_CAN_PRIO_CHANNEL
                             | THINGSET_CAN_SOURCE_SET(ts_can->node_addr);

    ret = isotp_bind(&ts_can->recv_ctx, ts_can->dev, &ts_can->rx_addr, &ts_can->tx_addr, &fc_opts,
                     timeout);
    if (ret != ISOTP_N_OK) {
        LOG_DBG("Failed to bind to rx ID %d [%d]", ts_can->rx_addr.ext_id, ret);
        return -EIO;
    }

    rx_len = 0;
    do {
        /* isotp_recv not suitable because it does not indicate if the buffer was too small */
        rem_len = isotp_recv_net(&ts_can->recv_ctx, &netbuf, timeout);
        if (rem_len < 0) {
            LOG_ERR("ISO-TP receiving error: %d", rem_len);
            break;
        }
        if (rx_len + netbuf->len <= rx_buf_size) {
            memcpy(&rx_buffer[rx_len], netbuf->data, netbuf->len);
        }
        rx_len += netbuf->len;
        net_buf_unref(netbuf);
    } while (rem_len);

    /* we need to unbind the receive ctx so that flow control frames are received in the send ctx */
    isotp_unbind(&ts_can->recv_ctx);

    if (rx_len > rx_buf_size) {
        LOG_ERR("ISO-TP RX buffer too small");
        return -ENOMEM;
    }
    else if (rx_len > 0 && rem_len == 0) {
        *source_addr = THINGSET_CAN_SOURCE_GET(ts_can->recv_ctx.rx_addr.ext_id);
        LOG_DBG("ISO-TP received %d bytes from addr %d", rx_len, *source_addr);
        return rx_len;
    }
    else if (rem_len == ISOTP_RECV_TIMEOUT) {
        return -EAGAIN;
    }
    else {
        return -EIO;
    }
}

int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr)
{
    if (!device_is_ready(ts_can->dev)) {
        return -ENODEV;
    }

    ts_can->tx_addr.ext_id = THINGSET_CAN_TYPE_CHANNEL | THINGSET_CAN_PRIO_CHANNEL
                             | THINGSET_CAN_TARGET_SET(target_addr)
                             | THINGSET_CAN_SOURCE_SET(ts_can->node_addr);

    ts_can->rx_addr.ext_id = THINGSET_CAN_TYPE_CHANNEL | THINGSET_CAN_PRIO_CHANNEL
                             | THINGSET_CAN_TARGET_SET(ts_can->node_addr)
                             | THINGSET_CAN_SOURCE_SET(target_addr);

    int ret = isotp_send(&ts_can->send_ctx, ts_can->dev, tx_buf, tx_len, &ts_can->tx_addr,
                         &ts_can->rx_addr, NULL, NULL);

    if (ret == ISOTP_N_OK) {
        return 0;
    }
    else {
        LOG_ERR("Error sending data to addr %d: %d", target_addr, ret);
        return -EIO;
    }
}

int thingset_can_process_inst(struct thingset_can *ts_can, k_timeout_t timeout)
{
    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
    uint8_t external_addr;
    int tx_len, rx_len;
    int err;

    rx_len = thingset_can_receive_inst(ts_can, ts_can->rx_buffer, sizeof(ts_can->rx_buffer),
                                       &external_addr, timeout);
    if (rx_len == -EAGAIN) {
        return -EAGAIN;
    }

    k_sem_take(&sbuf->lock, K_FOREVER);

    if (rx_len > 0) {
        tx_len = thingset_process_message(&ts, ts_can->rx_buffer, rx_len, sbuf->data, sbuf->size);
    }
    else if (rx_len == -ENOMEM) {
        sbuf->data[0] = THINGSET_ERR_REQUEST_TOO_LARGE;
        tx_len = 1;
    }
    else {
        sbuf->data[0] = THINGSET_ERR_INTERNAL_SERVER_ERR;
        tx_len = 1;
    }

    /*
     * Below delay gives the requesting side some more time to switch between sending and
     * receiving mode.
     *
     * ToDo: Improve Zephyr ISO-TP implementation to support sending and receiving simultaneously.
     */
    k_sleep(K_MSEC(CONFIG_THINGSET_CAN_RESPONSE_DELAY));

    if (tx_len > 0) {
        err = thingset_can_send_inst(ts_can, sbuf->data, tx_len, external_addr);
        if (err == -ENODEV) {
            LOG_ERR("CAN processing stopped because device not ready");
            k_sem_give(&sbuf->lock);
            return err;
        }
    }

    k_sem_give(&sbuf->lock);
    return 0;
}

int thingset_can_init_inst(struct thingset_can *ts_can, const struct device *can_dev)
{
    struct can_frame tx_frame = {
        .flags = CAN_FRAME_IDE,
    };
    int filter_id;
    int err;

    if (!device_is_ready(can_dev)) {
        LOG_ERR("CAN device not ready");
        return -ENODEV;
    }

    k_work_init_delayable(&ts_can->reporting_work, thingset_can_report_tx_handler);
    k_work_init_delayable(&ts_can->addr_claim_work, thingset_can_addr_claim_tx_handler);

    ts_can->dev = can_dev;

    /* set initial address (will be changed if already used on the bus) */
    if (ts_can->node_addr < 1 || ts_can->node_addr > THINGSET_CAN_ADDR_MAX) {
        ts_can->node_addr = 1;
    }

    k_event_init(&ts_can->events);

    can_start(ts_can->dev);

    filter_id =
        can_add_rx_filter(ts_can->dev, thingset_can_addr_claim_rx_cb, ts_can, &addr_claim_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add addr_claim filter: %d", filter_id);
        return filter_id;
    }

    while (1) {
        k_event_clear(&ts_can->events, EVENT_ADDRESS_ALREADY_USED);

        /* send out address discovery frame */
        uint8_t rand = sys_rand32_get() & 0xFF;
        tx_frame.id = THINGSET_CAN_PRIO_NETWORK_MGMT | THINGSET_CAN_TYPE_NETWORK
                      | THINGSET_CAN_RAND_SET(rand) | THINGSET_CAN_TARGET_SET(ts_can->node_addr)
                      | THINGSET_CAN_SOURCE_SET(THINGSET_CAN_ADDR_ANONYMOUS);
        tx_frame.dlc = 0;
        err = can_send(ts_can->dev, &tx_frame, K_MSEC(10), NULL, NULL);
        if (err != 0) {
            k_sleep(K_MSEC(100));
            continue;
        }

        /* wait 500 ms for address claim message from other node */
        uint32_t event =
            k_event_wait(&ts_can->events, EVENT_ADDRESS_ALREADY_USED, false, K_MSEC(500));
        if (event & EVENT_ADDRESS_ALREADY_USED) {
            /* try again with new random node_addr between 0x01 and 0xFD */
            ts_can->node_addr = 1 + sys_rand32_get() % THINGSET_CAN_ADDR_MAX;
            LOG_WRN("Node addr already in use, trying 0x%.2X", ts_can->node_addr);
        }
        else {
            struct can_bus_err_cnt err_cnt_before;
            can_get_state(ts_can->dev, NULL, &err_cnt_before);

            thingset_sdk_reschedule_work(&ts_can->addr_claim_work, K_NO_WAIT);

            event = k_event_wait(&ts_can->events, EVENT_ADDRESS_CLAIM_MSG_SENT, false, K_MSEC(100));
            if (!(event & EVENT_ADDRESS_CLAIM_MSG_SENT)) {
                k_sleep(K_MSEC(100));
                continue;
            }

            struct can_bus_err_cnt err_cnt_after;
            can_get_state(ts_can->dev, NULL, &err_cnt_after);

            if (err_cnt_after.tx_err_cnt <= err_cnt_before.tx_err_cnt) {
                /* address claiming is finished */
                k_event_post(&ts_can->events, EVENT_ADDRESS_CLAIMING_FINISHED);
                LOG_INF("Using CAN node address 0x%.2X", ts_can->node_addr);
                break;
            }

            /* Continue the loop in the very unlikely case of a collision because two nodes with
             * different EUI-64 tried to claim the same node address at exactly the same time.
             */
        }
    }

#if CONFIG_THINGSET_STORAGE
    /* save node address as init value for next boot-up */
    thingset_storage_save_queued();
#endif

    ts_can->rx_addr.ide = 1;
    ts_can->rx_addr.use_ext_addr = 0;   /* Normal ISO-TP addressing (using only CAN ID) */
    ts_can->rx_addr.use_fixed_addr = 1; /* enable SAE J1939 compatible addressing */

    ts_can->tx_addr.ide = 1;
    ts_can->tx_addr.use_ext_addr = 0;
    ts_can->tx_addr.use_fixed_addr = 1;

    struct can_filter addr_discovery_filter = {
        .id = THINGSET_CAN_TYPE_NETWORK | THINGSET_CAN_SOURCE_SET(THINGSET_CAN_ADDR_ANONYMOUS)
              | THINGSET_CAN_TARGET_SET(ts_can->node_addr),
        .mask = THINGSET_CAN_TYPE_MASK | THINGSET_CAN_TARGET_MASK | THINGSET_CAN_TARGET_MASK,
        .flags = CAN_FILTER_DATA | CAN_FILTER_IDE,
    };
    filter_id = can_add_rx_filter(ts_can->dev, thingset_can_addr_discovery_rx_cb, ts_can,
                                  &addr_discovery_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add addr_discovery filter: %d", filter_id);
        return filter_id;
    }

    thingset_sdk_reschedule_work(&ts_can->reporting_work, K_NO_WAIT);

    return 0;
}

int thingset_can_set_report_rx_callback_inst(struct thingset_can *ts_can,
                                             thingset_can_report_rx_callback_t rx_cb)
{
    if (!device_is_ready(ts_can->dev)) {
        return -ENODEV;
    }

    if (rx_cb == NULL) {
        return -EINVAL;
    }

    ts_can->report_rx_cb = rx_cb;

    int filter_id =
        can_add_rx_filter(ts_can->dev, thingset_can_report_rx_cb, ts_can, &report_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add report filter: %d", filter_id);
        return filter_id;
    }

    return 0;
}

#ifndef CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES

#if DT_NODE_EXISTS(DT_CHOSEN(thingset_can))
#define CAN_DEVICE_NODE DT_CHOSEN(thingset_can)
#else
#define CAN_DEVICE_NODE DT_CHOSEN(zephyr_canbus)
#endif

static const struct device *can_dev = DEVICE_DT_GET(CAN_DEVICE_NODE);

static struct thingset_can ts_can_single = {
    .dev = DEVICE_DT_GET(CAN_DEVICE_NODE),
    .node_addr = 1, /* initialize with valid default address */
};

THINGSET_ADD_ITEM_UINT8(TS_ID_NET, TS_ID_NET_CAN_NODE_ADDR, "pCANNodeAddr",
                        &ts_can_single.node_addr, THINGSET_ANY_RW, TS_SUBSET_NVM);

int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr)
{
    return thingset_can_send_inst(&ts_can_single, tx_buf, tx_len, target_addr);
}

int thingset_can_set_report_rx_callback(thingset_can_report_rx_callback_t rx_cb)
{
    return thingset_can_set_report_rx_callback_inst(&ts_can_single, rx_cb);
}

static void thingset_can_thread()
{
    thingset_can_init_inst(&ts_can_single, can_dev);

    while (true) {
        thingset_can_process_inst(&ts_can_single, K_FOREVER);
    }
}

K_THREAD_DEFINE(thingset_can, CONFIG_THINGSET_CAN_THREAD_STACK_SIZE, thingset_can_thread, NULL,
                NULL, NULL, CONFIG_THINGSET_CAN_THREAD_PRIORITY, 0, 0);

#endif /* !CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES */
