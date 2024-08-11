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
#include <zephyr/net/buf.h>
#include <zephyr/random/random.h>

#include <thingset.h>
#include <thingset/can.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

LOG_MODULE_REGISTER(thingset_can, CONFIG_THINGSET_SDK_LOG_LEVEL);

extern uint8_t eui64[8];

#define EVENT_ADDRESS_CLAIM_MSG_SENT    BIT(1)
#define EVENT_ADDRESS_CLAIMING_FINISHED BIT(2)
#define EVENT_ADDRESS_ALREADY_USED      BIT(3)
#define EVENT_ADDRESS_CLAIM_TIMED_OUT   BIT(4)

#ifdef CONFIG_THINGSET_CAN_ITEM_RX
static const struct can_filter sf_report_filter = {
    .id = THINGSET_CAN_TYPE_SF_REPORT,
    .mask = THINGSET_CAN_TYPE_MASK,
    .flags = CAN_FILTER_IDE,
};
#endif /* CONFIG_THINGSET_CAN_ITEM_RX */

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
static const struct can_filter mf_report_filter = {
    .id = THINGSET_CAN_TYPE_MF_REPORT,
    .mask = THINGSET_CAN_TYPE_MASK,
    .flags = CAN_FILTER_IDE,
};
#endif /* CONFIG_THINGSET_CAN_REPORT_RX */

static const struct isotp_fast_opts fc_opts = {
    .bs = 8, /* block size */
    .stmin = CONFIG_THINGSET_CAN_FRAME_SEPARATION_TIME,
    .addressing_mode = ISOTP_FAST_ADDRESSING_MODE_CUSTOM,
#ifdef CONFIG_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF,
#endif
};

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
struct thingset_can_rx_context
{
    uint8_t src_addr;
    uint8_t msg;
    uint8_t seq;
    bool started;
};

NET_BUF_POOL_DEFINE(thingset_can_rx_buffer_pool, CONFIG_THINGSET_CAN_REPORT_RX_NUM_BUFFERS,
                    CONFIG_THINGSET_CAN_REPORT_RX_BUFFER_SIZE,
                    sizeof(struct thingset_can_rx_context), NULL);

/* Simple hashtable (key is src_addr % number of buckets) to speed up buffer retrival */
static sys_slist_t rx_buf_lookup[CONFIG_THINGSET_CAN_REPORT_RX_BUCKETS];

static struct net_buf *thingset_can_get_rx_buf(uint8_t src_addr)
{
    sys_slist_t *list = &rx_buf_lookup[src_addr % CONFIG_THINGSET_CAN_REPORT_RX_BUCKETS];

    sys_snode_t *pnode;
    struct net_buf *buffer;
    SYS_SLIST_FOR_EACH_NODE(list, pnode)
    {
        buffer = CONTAINER_OF(pnode, struct net_buf, node);
        struct thingset_can_rx_context *context =
            (struct thingset_can_rx_context *)buffer->user_data;
        if (context != NULL && context->src_addr == src_addr) {
            LOG_DBG("Found existing RX buffer for sender %x", src_addr);
            return buffer;
        }
    }

    buffer = net_buf_alloc(&thingset_can_rx_buffer_pool, K_NO_WAIT);
    if (buffer != NULL) {
        struct thingset_can_rx_context *context =
            (struct thingset_can_rx_context *)buffer->user_data;
        context->src_addr = src_addr;
        context->seq = 0;
        sys_slist_append(list, &buffer->node);
        LOG_DBG("Created new RX buffer for sender %x", src_addr);
        return buffer;
    }

    return NULL;
}

static void thingset_can_free_rx_buf(struct net_buf *buffer)
{
    struct thingset_can_rx_context *context = (struct thingset_can_rx_context *)buffer->user_data;
    sys_slist_t *list = &rx_buf_lookup[context->src_addr % CONFIG_THINGSET_CAN_REPORT_RX_BUCKETS];
    sys_slist_find_and_remove(list, &buffer->node);
    LOG_DBG("Releasing RX buffer of length %d for sender %x", buffer->len, context->src_addr);
    net_buf_unref(buffer);
}
#endif /* CONFIG_THINGSET_CAN_REPORT_RX */

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
        .id = THINGSET_CAN_TYPE_NETWORK | THINGSET_CAN_PRIO_NETWORK_MGMT
#ifdef CONFIG_THINGSET_CAN_ROUTING_BUSES
              | THINGSET_CAN_TARGET_BUS_SET(ts_can->route)
              | THINGSET_CAN_SOURCE_BUS_SET(ts_can->route)
#else /* CONFIG_THINGSET_CAN_ROUTING_BRIDGES */
              | THINGSET_CAN_BRIDGE_SET(ts_can->route)
#endif
              | THINGSET_CAN_TARGET_SET(THINGSET_CAN_ADDR_BROADCAST)
              | THINGSET_CAN_SOURCE_SET(ts_can->node_addr),
        .flags = CAN_FRAME_IDE,
        .dlc = sizeof(eui64),
    };
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
            data[5], data[6], data[7]);

    uint8_t source_addr = THINGSET_CAN_SOURCE_GET(frame->id);

    if (ts_can->node_addr == source_addr) {
        k_event_post(&ts_can->events, EVENT_ADDRESS_ALREADY_USED);
    }

    if (ts_can->addr_claim_callback != NULL) {
        ts_can->addr_claim_callback(data, source_addr);
    }

    /* Optimization: store in internal database to exclude from potentially available addresses */
}

#ifdef CONFIG_THINGSET_CAN_ITEM_RX
static void thingset_can_item_rx_cb(const struct device *dev, struct can_frame *frame,
                                    void *user_data)
{
    struct thingset_can *ts_can = user_data;
    uint16_t data_id = THINGSET_CAN_DATA_ID_GET(frame->id);
    uint8_t source_addr = THINGSET_CAN_SOURCE_GET(frame->id);

    ts_can->item_rx_cb(data_id, frame->data, can_dlc_to_bytes(frame->dlc), source_addr);
}
#endif /* CONFIG_THINGSET_CAN_ITEM_RX */

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
static void thingset_can_report_rx_cb(const struct device *dev, struct can_frame *frame,
                                      void *user_data)
{
    struct thingset_can *ts_can = user_data;
    uint8_t source_addr = THINGSET_CAN_SOURCE_GET(frame->id);
    uint8_t msg_no = THINGSET_CAN_MSG_NO_GET(frame->id);
    uint8_t seq = THINGSET_CAN_SEQ_NO_GET(frame->id);

    struct net_buf *buffer = thingset_can_get_rx_buf(source_addr);
    if (buffer != NULL) {
        struct thingset_can_rx_context *context =
            (struct thingset_can_rx_context *)buffer->user_data;

        if ((frame->id & THINGSET_CAN_MF_TYPE_MASK) == THINGSET_CAN_MF_TYPE_SINGLE
            || (frame->id & THINGSET_CAN_MF_TYPE_MASK) == THINGSET_CAN_MF_TYPE_FIRST)
        {
            context->msg = msg_no;
            context->started = true;
        }
        else if (context->msg != msg_no) {
            LOG_WRN("Out-of-message frame received");
            thingset_can_free_rx_buf(buffer);
            return;
        }
        else if (!context->started) {
            LOG_WRN("Missing first frame");
            thingset_can_free_rx_buf(buffer);
            return;
        }

        if ((context->seq & 0xF) == seq) {
            int chunk_len = can_dlc_to_bytes(frame->dlc);
            if (buffer->len + chunk_len > buffer->size) {
                LOG_WRN("Discarded too large report from 0x%X", source_addr);
                thingset_can_free_rx_buf(buffer);
                return;
            }
            uint8_t *buf = net_buf_add(buffer, chunk_len);
            LOG_DBG("Reassembling %d bytes from ID 0x%08X", chunk_len, frame->id);
            memcpy(buf, frame->data, chunk_len);
            if ((frame->id & THINGSET_CAN_MF_TYPE_MASK) == THINGSET_CAN_MF_TYPE_SINGLE
                || (frame->id & THINGSET_CAN_MF_TYPE_MASK) == THINGSET_CAN_MF_TYPE_LAST)
            {
                LOG_DBG("Finished; dispatching %d bytes from node %x", buffer->len, source_addr);
                ts_can->report_rx_cb(buffer->data, buffer->len, source_addr);
                thingset_can_free_rx_buf(buffer);
            }

            context->seq++;
        }
        else {
            /* out-of-sequence frame received, so free the buffer */
            LOG_WRN("Out-of-sequence frame received");
            thingset_can_free_rx_buf(buffer);
        }
    }
}
#endif /* CONFIG_THINGSET_CAN_REPORT_RX */

static void thingset_can_report_tx_cb(const struct device *dev, int error, void *user_data)
{
    struct thingset_can *ts_can = (struct thingset_can *)user_data;

    k_sem_give(&ts_can->report_tx_sem);
}

int thingset_can_send_report_inst(struct thingset_can *ts_can, const char *path,
                                  enum thingset_data_format format)
{
    int len, ret = 0;
    int pos = 0;
    int chunk_len;
    uint8_t seq = 0;
    bool end = false;

    struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
    k_sem_take(&tx_buf->lock, K_FOREVER);

    k_sem_reset(&ts_can->report_tx_sem);

    len = thingset_report_path(&ts, tx_buf->data, tx_buf->size, path, format);
    if (len <= 0) {
        goto out;
    }

    struct can_frame frame = {
        .flags = CAN_FRAME_IDE | (IS_ENABLED(CONFIG_CAN_FD_MODE) ? CAN_FRAME_FDF : 0),
    };

    do {
        uint32_t mf_type;
        if (len - pos > CAN_MAX_DLEN) {
            chunk_len = CAN_MAX_DLEN;
            mf_type = (pos == 0) ? THINGSET_CAN_MF_TYPE_FIRST : THINGSET_CAN_MF_TYPE_CONSEC;
        }
        else {
            chunk_len = len - pos;
            end = true;
            mf_type = (pos == 0) ? THINGSET_CAN_MF_TYPE_SINGLE : THINGSET_CAN_MF_TYPE_LAST;
        }
        memcpy(frame.data, tx_buf->data + pos, chunk_len);
        frame.id = THINGSET_CAN_PRIO_REPORT_LOW | THINGSET_CAN_TYPE_MF_REPORT
                   | THINGSET_CAN_MSG_NO_SET(ts_can->msg_no) | mf_type
                   | THINGSET_CAN_SEQ_NO_SET(seq) | THINGSET_CAN_SOURCE_SET(ts_can->node_addr);
        frame.dlc = can_bytes_to_dlc(chunk_len);
        if (end && IS_ENABLED(CONFIG_CAN_FD_MODE)) {
            /* pad message with empty bytes */
            size_t frame_len = can_dlc_to_bytes(frame.dlc);
            if (frame_len > chunk_len) {
                for (unsigned int i = 0; i < frame_len - chunk_len; i++) {
                    frame.data[chunk_len + i] = 0x00;
                }
            }
        }

        ret = can_send(ts_can->dev, &frame, K_MSEC(CONFIG_THINGSET_CAN_REPORT_SEND_TIMEOUT),
                       thingset_can_report_tx_cb, ts_can);
        if (ret == -EAGAIN) {
            LOG_DBG("Error sending CAN frame with ID 0x%X", frame.id);
            break;
        }

        /* wait until frame was actually sent to ensure message order */
        ret = k_sem_take(&ts_can->report_tx_sem, K_MSEC(100));
        if (ret != 0) {
            LOG_DBG("Sending CAN frame with ID 0x%X timed out", frame.id);
            break;
        }

        k_sleep(K_MSEC(CONFIG_THINGSET_CAN_FRAME_SEPARATION_TIME));

        seq++;
        pos += chunk_len;
    } while (len - pos > 0);

    ts_can->msg_no++;

out:
    k_sem_give(&tx_buf->lock);
    return ret;
}

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
static void thingset_can_live_reporting_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct thingset_can *ts_can = CONTAINER_OF(dwork, struct thingset_can, live_reporting_work);

    if (live_reporting_enable) {
        thingset_can_send_report_inst(ts_can, TS_NAME_SUBSET_LIVE, THINGSET_BIN_IDS_VALUES);
    }

    ts_can->next_live_report_time += 1000 * live_reporting_period;
    if (ts_can->next_live_report_time <= k_uptime_get()) {
        /* ensure proper initialization of next_live_report_time */
        ts_can->next_live_report_time = k_uptime_get() + 1000 * live_reporting_period;
    }

    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(ts_can->next_live_report_time));
}
#endif /* CONFIG_THINGSET_SUBSET_LIVE_METRICS */

#ifdef CONFIG_THINGSET_CAN_CONTROL_REPORTING
static void thingset_can_item_tx_cb(const struct device *dev, int error, void *user_data)
{
    /* Do nothing: Single-frame reports are fire and forget. */
}

static void thingset_can_control_reporting_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct thingset_can *ts_can = CONTAINER_OF(dwork, struct thingset_can, control_reporting_work);
    int data_len = 0;
    int err;

    struct can_frame frame = {
        .flags = CAN_FRAME_IDE,
    };
    struct shared_buffer *sbuf = thingset_sdk_shared_buffer();

    struct thingset_data_object *obj = NULL;
    while (live_reporting_enable
           && (obj = thingset_iterate_subsets(&ts, CONFIG_THINGSET_CAN_CONTROL_SUBSET, obj))
                  != NULL)
    {
        k_sem_take(&sbuf->lock, K_FOREVER);
        data_len = thingset_export_item(&ts, sbuf->data, sbuf->size, obj, THINGSET_BIN_VALUES_ONLY);
        if (data_len > CAN_MAX_DLEN) {
            LOG_WRN("Value of data item %x exceeds single CAN frame payload size", obj->id);
            k_sem_give(&sbuf->lock);
        }
        else if (data_len > 0) {
            memcpy(frame.data, sbuf->data, data_len);
            k_sem_give(&sbuf->lock);
            frame.id = THINGSET_CAN_TYPE_SF_REPORT | THINGSET_CAN_PRIO_REPORT_LOW
                       | THINGSET_CAN_DATA_ID_SET(obj->id)
                       | THINGSET_CAN_SOURCE_SET(ts_can->node_addr);
#ifdef CONFIG_CAN_FD_MODE
            frame.flags |= CAN_FRAME_FDF;
#endif
            frame.dlc = can_bytes_to_dlc(data_len);
            err = can_send(ts_can->dev, &frame, K_MSEC(CONFIG_THINGSET_CAN_REPORT_SEND_TIMEOUT),
                           thingset_can_item_tx_cb, NULL);
            if (err != 0) {
                LOG_DBG("Error sending CAN frame with ID %x", frame.id);
            }
#ifdef CONFIG_CAN_FD_MODE
            frame.flags &= ~CAN_FRAME_FDF;
#endif
        }
        else {
            k_sem_give(&sbuf->lock);
        }
        obj++; /* continue with object behind current one */
    }

    ts_can->next_control_report_time += CONFIG_THINGSET_CAN_CONTROL_REPORTING_PERIOD;
    if (ts_can->next_control_report_time <= k_uptime_get()) {
        /* ensure proper initialization of next_control_report_time */
        ts_can->next_control_report_time =
            k_uptime_get() + CONFIG_THINGSET_CAN_CONTROL_REPORTING_PERIOD;
    }

    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(ts_can->next_control_report_time));
}
#endif

void thingset_can_reset_request_response(struct thingset_can_request_response *rr)
{
    rr->callback = NULL;
    rr->cb_arg = NULL;
    rr->can_id = 0;
    k_timer_stop(&rr->timer);
    k_sem_give(&rr->sem);
}

static struct isotp_fast_addr thingset_can_get_tx_addr(const struct isotp_fast_addr *rx_addr)
{
    return (struct isotp_fast_addr){
        .ext_id = (rx_addr->ext_id & 0x1F000000)
#ifdef CONFIG_THINGSET_CAN_ROUTING_BUSES
                  | THINGSET_CAN_TARGET_BUS_SET(THINGSET_CAN_SOURCE_BUS_GET(rx_addr->ext_id))
                  | THINGSET_CAN_SOURCE_BUS_SET(THINGSET_CAN_TARGET_BUS_GET(rx_addr->ext_id))
#else /* CONFIG_THINGSET_CAN_ROUTING_BRIDGES */
                  | THINGSET_CAN_BRIDGE_SET(THINGSET_CAN_BRIDGE_GET(rx_addr->ext_id))
#endif
                  | THINGSET_CAN_SOURCE_SET(THINGSET_CAN_TARGET_GET(rx_addr->ext_id))
                  | THINGSET_CAN_TARGET_SET(THINGSET_CAN_SOURCE_GET(rx_addr->ext_id)),
    };
}

static void thingset_can_reqresp_timeout_handler(struct k_timer *timer)
{
    struct thingset_can_request_response *rr =
        CONTAINER_OF(timer, struct thingset_can_request_response, timer);
    rr->callback(NULL, 0, 0, -ETIMEDOUT, THINGSET_CAN_SOURCE_GET(rr->can_id), rr->cb_arg);
    thingset_can_reset_request_response(rr);
}

int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr, uint8_t route,
                           thingset_can_reqresp_callback_t callback, void *callback_arg,
                           k_timeout_t timeout)
{
    if (!device_is_ready(ts_can->dev)) {
        return -ENODEV;
    }

    struct isotp_fast_addr tx_addr = {
        .ext_id = THINGSET_CAN_TYPE_REQRESP | THINGSET_CAN_PRIO_REQRESP
#ifdef CONFIG_THINGSET_CAN_ROUTING_BUSES
                  | THINGSET_CAN_SOURCE_BUS_SET(ts_can->route) | THINGSET_CAN_TARGET_BUS_SET(route)
#else /* CONFIG_THINGSET_CAN_ROUTING_BRIDGES */
                  | THINGSET_CAN_BRIDGE_SET(route)
#endif
                  | THINGSET_CAN_SOURCE_SET(ts_can->node_addr)
                  | THINGSET_CAN_TARGET_SET(target_addr),
    };

    if (callback != NULL) {
        if (k_sem_take(&ts_can->request_response.sem, timeout) != 0) {
            return -ETIMEDOUT;
        }

        ts_can->request_response.callback = callback;
        ts_can->request_response.cb_arg = callback_arg;
        k_timer_init(&ts_can->request_response.timer, thingset_can_reqresp_timeout_handler, NULL);
        k_timer_start(&ts_can->request_response.timer, timeout, K_NO_WAIT);
        ts_can->request_response.can_id = thingset_can_get_tx_addr(&tx_addr).ext_id;
    }

    int ret = isotp_fast_send(&ts_can->ctx, tx_buf, tx_len, tx_addr, ts_can);

    if (ret == ISOTP_N_OK) {
        return 0;
    }
    else {
        LOG_ERR("Error sending data to addr 0x%X: %d", target_addr, ret);
        return -EIO;
    }
}

static void thingset_can_reqresp_recv_callback(struct net_buf *buffer, int rem_len,
                                               struct isotp_fast_addr addr, void *arg)
{
    struct thingset_can *ts_can = arg;

    if (rem_len < 0) {
        LOG_ERR("RX error %d", rem_len);
    }

    if (rem_len == 0) {
        size_t len = net_buf_frags_len(buffer);
        net_buf_linearize(ts_can->rx_buffer, sizeof(ts_can->rx_buffer), buffer, 0, len);
        if (ts_can->request_response.callback != NULL
            && ts_can->request_response.can_id == addr.ext_id)
        {
            ts_can->request_response.callback(ts_can->rx_buffer, len, 0, 0,
                                              (uint8_t)(addr.ext_id & 0xFF),
                                              ts_can->request_response.cb_arg);
            thingset_can_reset_request_response(&ts_can->request_response);
        }
        else {
            struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
            k_sem_take(&sbuf->lock, K_FOREVER);
            int tx_len =
                thingset_process_message(&ts, ts_can->rx_buffer, len, sbuf->data, sbuf->size);
            if (tx_len > 0) {
                uint8_t target_addr = THINGSET_CAN_SOURCE_GET(addr.ext_id);
                uint8_t route = IS_ENABLED(CONFIG_THINGSET_CAN_ROUTING_BUSES)
                                    ? THINGSET_CAN_SOURCE_BUS_GET(addr.ext_id)
                                    : THINGSET_CAN_BRIDGE_GET(addr.ext_id);
                int err = thingset_can_send_inst(ts_can, sbuf->data, tx_len, target_addr, route,
                                                 NULL, NULL, K_NO_WAIT);
                if (err != 0) {
                    k_sem_give(&sbuf->lock);
                }
            }
            else {
                k_sem_give(&sbuf->lock);
            }
        }
    }
}

static void thingset_can_reqresp_recv_error_callback(int8_t error, struct isotp_fast_addr addr,
                                                     void *arg)
{
    LOG_ERR("RX error %d", error);
}

static void thingset_can_reqresp_sent_callback(int result, void *arg)
{
    struct thingset_can *ts_can = arg;
    if (ts_can->request_response.callback != NULL) {
        ts_can->request_response.callback(NULL, 0, 0, result,
                                          THINGSET_CAN_SOURCE_GET(ts_can->request_response.can_id),
                                          ts_can->request_response.cb_arg);
        thingset_can_reset_request_response(&ts_can->request_response);
        if (result == 0) {
            /* maintain unlocking semantics of previous iteration of this code */
            struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
            k_sem_give(&sbuf->lock);
        }
    }
    else {
        struct shared_buffer *sbuf = thingset_sdk_shared_buffer();
        k_sem_give(&sbuf->lock);
    }
}

static void thingset_can_timeout_timer_expired(struct k_timer *timer)
{
    struct thingset_can *ts_can = CONTAINER_OF(timer, struct thingset_can, timeout_timer);
    k_event_set(&ts_can->events, EVENT_ADDRESS_CLAIM_TIMED_OUT);
}

static void thingset_can_timeout_timer_stopped(struct k_timer *timer)
{
    ARG_UNUSED(timer);
}

int thingset_can_init_inst(struct thingset_can *ts_can, const struct device *can_dev,
                           uint8_t bus_number, k_timeout_t timeout)
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

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
    for (int i = 0; i < CONFIG_THINGSET_CAN_REPORT_RX_BUCKETS; i++) {
        sys_slist_init(&rx_buf_lookup[i]);
    }
#endif
    k_sem_init(&ts_can->request_response.sem, 1, 1);
    k_sem_init(&ts_can->report_tx_sem, 0, 1);
    k_timer_init(&ts_can->timeout_timer, thingset_can_timeout_timer_expired,
                 thingset_can_timeout_timer_stopped);

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
    k_work_init_delayable(&ts_can->live_reporting_work, thingset_can_live_reporting_handler);
#endif
#ifdef CONFIG_THINGSET_CAN_CONTROL_REPORTING
    k_work_init_delayable(&ts_can->control_reporting_work, thingset_can_control_reporting_handler);
#endif
    k_work_init_delayable(&ts_can->addr_claim_work, thingset_can_addr_claim_tx_handler);

    ts_can->dev = can_dev;
    ts_can->route = bus_number;

    /* set initial address (will be changed if already used on the bus) */
    if (ts_can->node_addr < THINGSET_CAN_ADDR_MIN || ts_can->node_addr > THINGSET_CAN_ADDR_MAX) {
        ts_can->node_addr = THINGSET_CAN_ADDR_MIN;
    }

    k_event_init(&ts_can->events);
    k_timer_start(&ts_can->timeout_timer, timeout, K_NO_WAIT);

#ifdef CONFIG_CAN_FD_MODE
    can_mode_t supported_modes;
    err = can_get_capabilities(can_dev, &supported_modes);
    if (err == 0 && (supported_modes & CAN_MODE_FD) != 0) {
        err = can_set_mode(ts_can->dev, CAN_MODE_FD);
        if (err == 0) {
            LOG_DBG("Enabled CAN-FD mode");
        }
        else {
            LOG_ERR("Failed to enable CAN-FD mode");
            return -ENODEV;
        }
    }
    else {
        LOG_ERR("CAN device does not support CAN-FD; recompile with CAN_FD_MODE set to false.");
        /* there is no point continuing, as we will still assume a 64-byte payload everywhere */
        return -ENODEV;
    }
#endif

    can_start(ts_can->dev);

    struct can_filter addr_claim_filter = {
        .id = THINGSET_CAN_TYPE_NETWORK | THINGSET_CAN_TARGET_SET(THINGSET_CAN_ADDR_BROADCAST),
        .mask = THINGSET_CAN_TYPE_MASK | THINGSET_CAN_TARGET_MASK,
        .flags = CAN_FILTER_IDE,
    };

#ifdef CONFIG_THINGSET_CAN_ROUTING_BUSES
    addr_claim_filter.id |=
        THINGSET_CAN_TARGET_BUS_SET(bus_number) | THINGSET_CAN_SOURCE_BUS_SET(bus_number);
    addr_claim_filter.mask |= THINGSET_CAN_TARGET_BUS_MASK | THINGSET_CAN_SOURCE_BUS_MASK;
#elif defined(CONFIG_THINGSET_CAN_ROUTING_BRIDGES)
    addr_claim_filter.id |= THINGSET_CAN_BRIDGE_SET(bus_number);
    addr_claim_filter.mask |= THINGSET_CAN_BRIDGE_MASK;
#endif

    filter_id =
        can_add_rx_filter(ts_can->dev, thingset_can_addr_claim_rx_cb, ts_can, &addr_claim_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add addr_claim filter: %d", filter_id);
        return filter_id;
    }

    while (1) {
        k_event_clear(&ts_can->events, EVENT_ADDRESS_CLAIM_MSG_SENT
                                           | EVENT_ADDRESS_CLAIMING_FINISHED
                                           | EVENT_ADDRESS_ALREADY_USED);

        /* send out address discovery frame */
        uint8_t rand = sys_rand32_get() & 0xFF;
        tx_frame.id = THINGSET_CAN_PRIO_NETWORK_MGMT | THINGSET_CAN_TYPE_NETWORK
                      | THINGSET_CAN_RAND_SET(rand) | THINGSET_CAN_TARGET_SET(ts_can->node_addr)
                      | THINGSET_CAN_SOURCE_SET(THINGSET_CAN_ADDR_ANONYMOUS);
        tx_frame.dlc = 0;
        err = can_send(ts_can->dev, &tx_frame, K_MSEC(10), thingset_can_addr_claim_tx_cb, ts_can);
        if (err != 0) {
            k_sleep(K_MSEC(100));
            continue;
        }

        /* wait 500 ms for address claim message from other node */
        uint32_t event = k_event_wait(&ts_can->events,
                                      EVENT_ADDRESS_ALREADY_USED | EVENT_ADDRESS_CLAIM_TIMED_OUT,
                                      false, K_MSEC(500));
        if (event & EVENT_ADDRESS_ALREADY_USED) {
            /* try again with new random node_addr between 0x01 and 0xFD */
            ts_can->node_addr =
                THINGSET_CAN_ADDR_MIN
                + sys_rand32_get() % (THINGSET_CAN_ADDR_MAX - THINGSET_CAN_ADDR_MIN);
            LOG_WRN("Node addr already in use, trying 0x%.2X", ts_can->node_addr);
        }
        else if (event & EVENT_ADDRESS_CLAIM_TIMED_OUT) {
            LOG_ERR("Address claim timed out");
            return -ETIMEDOUT;
        }
        else {
            struct can_bus_err_cnt err_cnt_before;
            can_get_state(ts_can->dev, NULL, &err_cnt_before);

            thingset_sdk_reschedule_work(&ts_can->addr_claim_work, K_NO_WAIT);

            event = k_event_wait(&ts_can->events,
                                 EVENT_ADDRESS_CLAIM_MSG_SENT | EVENT_ADDRESS_CLAIM_TIMED_OUT,
                                 false, K_MSEC(100));
            if (event & EVENT_ADDRESS_CLAIM_TIMED_OUT) {
                LOG_ERR("Address claim timed out");
                return -ETIMEDOUT;
            }
            else if (!(event & EVENT_ADDRESS_CLAIM_MSG_SENT)) {
                k_sleep(K_MSEC(100));
                continue;
            }

            struct can_bus_err_cnt err_cnt_after;
            can_get_state(ts_can->dev, NULL, &err_cnt_after);

            if (err_cnt_after.tx_err_cnt <= err_cnt_before.tx_err_cnt) {
                /* address claiming is finished */
                k_event_post(&ts_can->events, EVENT_ADDRESS_CLAIMING_FINISHED);
                k_timer_stop(&ts_can->timeout_timer);
                LOG_INF("Using CAN node address 0x%.2X on %s", ts_can->node_addr,
                        ts_can->dev->name);
                break;
            }

            /* Continue the loop in the very unlikely case of a collision because two nodes with
             * different EUI-64 tried to claim the same node address at exactly the same time.
             */
        }
    }

#if CONFIG_THINGSET_STORAGE
    /* save node address as init value for next boot-up */
    thingset_storage_save_queued(false);
#endif

    struct can_filter addr_discovery_filter = {
        .id = THINGSET_CAN_TYPE_NETWORK | THINGSET_CAN_SOURCE_SET(THINGSET_CAN_ADDR_ANONYMOUS)
              | THINGSET_CAN_TARGET_SET(ts_can->node_addr),
        .mask = THINGSET_CAN_TYPE_MASK | THINGSET_CAN_SOURCE_MASK | THINGSET_CAN_TARGET_MASK,
        .flags = CAN_FILTER_IDE,
    };
    filter_id = can_add_rx_filter(ts_can->dev, thingset_can_addr_discovery_rx_cb, ts_can,
                                  &addr_discovery_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add addr_discovery filter: %d", filter_id);
        return filter_id;
    }

    struct isotp_fast_addr rx_addr = {
        .ext_id = THINGSET_CAN_TYPE_REQRESP | THINGSET_CAN_PRIO_REQRESP
                  | THINGSET_CAN_TARGET_SET(ts_can->node_addr),
    };
    ts_can->ctx.get_tx_addr_callback = thingset_can_get_tx_addr;
    isotp_fast_bind(&ts_can->ctx, can_dev, rx_addr, &fc_opts, thingset_can_reqresp_recv_callback,
                    ts_can, thingset_can_reqresp_recv_error_callback,
                    thingset_can_reqresp_sent_callback);

#ifdef CONFIG_THINGSET_SUBSET_LIVE_METRICS
    thingset_sdk_reschedule_work(&ts_can->live_reporting_work, K_NO_WAIT);
#endif
#ifdef CONFIG_THINGSET_CAN_CONTROL_REPORTING
    thingset_sdk_reschedule_work(&ts_can->control_reporting_work, K_NO_WAIT);
#endif

    return 0;
}

void thingset_can_set_addr_claim_rx_callback_inst(struct thingset_can *ts_can,
                                                  thingset_can_addr_claim_rx_callback_t cb)
{
    ts_can->addr_claim_callback = cb;
}

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
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
        can_add_rx_filter(ts_can->dev, thingset_can_report_rx_cb, ts_can, &mf_report_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add packetized report filter: %d", filter_id);
        return filter_id;
    }

    return 0;
}
#endif /* CONFIG_THINGSET_CAN_REPORT_RX */

#ifdef CONFIG_THINGSET_CAN_ITEM_RX
int thingset_can_set_item_rx_callback_inst(struct thingset_can *ts_can,
                                           thingset_can_item_rx_callback_t rx_cb)
{
    if (!device_is_ready(ts_can->dev)) {
        return -ENODEV;
    }

    if (rx_cb == NULL) {
        return -EINVAL;
    }

    ts_can->item_rx_cb = rx_cb;

    int filter_id =
        can_add_rx_filter(ts_can->dev, thingset_can_item_rx_cb, ts_can, &sf_report_filter);
    if (filter_id < 0) {
        LOG_ERR("Unable to add report filter: %d", filter_id);
        return filter_id;
    }

    return 0;
}
#endif /* CONFIG_THINGSET_CAN_ITEM_RX */

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

int thingset_can_send_report(const char *path, enum thingset_data_format format)
{
    return thingset_can_send_report_inst(&ts_can_single, path, format);
}

int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr, uint8_t route,
                      thingset_can_reqresp_callback_t callback, void *callback_arg,
                      k_timeout_t timeout)
{
    return thingset_can_send_inst(&ts_can_single, tx_buf, tx_len, target_addr, route, callback,
                                  callback_arg, timeout);
}

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
int thingset_can_set_report_rx_callback(thingset_can_report_rx_callback_t rx_cb)
{
    return thingset_can_set_report_rx_callback_inst(&ts_can_single, rx_cb);
}
#endif

#ifdef CONFIG_THINGSET_CAN_ITEM_RX
int thingset_can_set_item_rx_callback(thingset_can_item_rx_callback_t rx_cb)
{
    return thingset_can_set_item_rx_callback_inst(&ts_can_single, rx_cb);
}
#endif

struct thingset_can *thingset_can_get_inst()
{
    return &ts_can_single;
}

static void thingset_can_thread()
{
    int err;

    LOG_DBG("Initialising ThingSet CAN");
    err = thingset_can_init_inst(&ts_can_single, can_dev, CONFIG_THINGSET_CAN_DEFAULT_ROUTE,
                                 K_FOREVER);
    if (err != 0) {
        LOG_ERR("Failed to init ThingSet CAN: %d", err);
        return;
    }
}

K_THREAD_DEFINE(thingset_can, CONFIG_THINGSET_CAN_THREAD_STACK_SIZE, thingset_can_thread, NULL,
                NULL, NULL, CONFIG_THINGSET_CAN_THREAD_PRIORITY, 0, 0);

#endif /* !CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES */
