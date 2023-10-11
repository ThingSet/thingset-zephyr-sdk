/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_CAN_H_
#define THINGSET_CAN_H_

#include "isotp_fast.h"
#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ThingSet addressing in 29-bit CAN ID
 *
 * Channel-based messages using ISO-TP:
 *
 *    28      26 25 24 23           16 15            8 7             0
 *   +----------+-----+---------------+---------------+---------------+
 *   | Priority | 0x0 |     bus ID    |  target addr  |  source addr  |
 *   +----------+-----+---------------+---------------+---------------+
 *
 *   Priority: 6
 *
 *   Bus ID:
 *     Set to 218 (0xDA) by default as suggested by ISO-TP standard (ISO 15765-2)
 *     for normal fixed addressing with N_TAtype = physical.
 *
 * Control and report messages (always single-frame):
 *
 *    28      26 25 24 23           16 15            8 7             0
 *   +----------+-----+---------------+---------------+---------------+
 *   | Priority | 0x2 | data ID (MSB) | data ID (LSB) |  source addr  |
 *   +----------+-----+---------------+---------------+---------------+
 *
 *   Priority:
 *     0 .. 3: High-priority control frames
 *     5, 7: Normal report frames for monitoring
 *
 * Network management (e.g. address claiming):
 *
 *    28      26 25 24 23           16 15            8 7             0
 *   +----------+-----+---------------+---------------+---------------+
 *   | Priority | 0x3 | variable byte |  target addr  |  source addr  |
 *   +----------+-----+---------------+---------------+---------------+
 *
 *   Priority: 4
 *
 *   Variable byte:
 *     - Random data for address discovery frame
 *     - Bus ID for address claiming frame (same as request/response)
 */

/* source and target addresses */
#define THINGSET_CAN_SOURCE_POS  (0U)
#define THINGSET_CAN_SOURCE_MASK (0xFF << THINGSET_CAN_SOURCE_POS)
#define THINGSET_CAN_SOURCE_SET(addr) \
    (((uint32_t)addr << THINGSET_CAN_SOURCE_POS) & THINGSET_CAN_SOURCE_MASK)
#define THINGSET_CAN_SOURCE_GET(id) \
    (((uint32_t)id & THINGSET_CAN_SOURCE_MASK) >> THINGSET_CAN_SOURCE_POS)

#define THINGSET_CAN_TARGET_POS  (8U)
#define THINGSET_CAN_TARGET_MASK (0xFF << THINGSET_CAN_TARGET_POS)
#define THINGSET_CAN_TARGET_SET(addr) \
    (((uint32_t)addr << THINGSET_CAN_TARGET_POS) & THINGSET_CAN_TARGET_MASK)
#define THINGSET_CAN_TARGET_GET(id) \
    (((uint32_t)id & THINGSET_CAN_TARGET_MASK) >> THINGSET_CAN_TARGET_POS)

#define THINGSET_CAN_ADDR_MAX       (0xFD)
#define THINGSET_CAN_ADDR_ANONYMOUS (0xFE)
#define THINGSET_CAN_ADDR_BROADCAST (0xFF)

/* data IDs for publication messages */
#define THINGSET_CAN_DATA_ID_POS  (8U)
#define THINGSET_CAN_DATA_ID_MASK (0xFFFF << THINGSET_CAN_DATA_ID_POS)
#define THINGSET_CAN_DATA_ID_SET(id) \
    (((uint32_t)id << THINGSET_CAN_DATA_ID_POS) & THINGSET_CAN_DATA_ID_MASK)
#define THINGSET_CAN_DATA_ID_GET(id) \
    (((uint32_t)id & THINGSET_CAN_DATA_ID_MASK) >> THINGSET_CAN_DATA_ID_POS)

/* bus ID for request/response messages */
#define THINGSET_CAN_BUS_ID_POS  (16U)
#define THINGSET_CAN_BUS_ID_MASK (0xFF << THINGSET_CAN_BUS_ID_POS)
#define THINGSET_CAN_BUS_ID_SET(id) \
    (((uint32_t)id << THINGSET_CAN_BUS_ID_POS) & THINGSET_CAN_BUS_ID_MASK)
#define THINGSET_CAN_BUS_ID_GET(id) \
    (((uint32_t)id & THINGSET_CAN_BUS_ID_MASK) >> THINGSET_CAN_BUS_ID_POS)
#define THINGSET_CAN_BUS_ID_DEFAULT (0xDA) // 218, N_TAtype = physical

/* random number for address discovery messages */
#define THINGSET_CAN_RAND_SET THINGSET_CAN_BUS_ID_SET
#define THINGSET_CAN_RAND_GET THINGSET_CAN_BUS_ID_GET

/* message types */
#define THINGSET_CAN_TYPE_POS  (24U)
#define THINGSET_CAN_TYPE_MASK (0x3 << THINGSET_CAN_TYPE_POS)

#define THINGSET_CAN_TYPE_CHANNEL           (0x0 << THINGSET_CAN_TYPE_POS)
#define THINGSET_CAN_TYPE_PACKETIZED_REPORT (0x1 << THINGSET_CAN_TYPE_POS)
#define THINGSET_CAN_TYPE_REPORT            (0x2 << THINGSET_CAN_TYPE_POS)
#define THINGSET_CAN_TYPE_NETWORK           (0x3 << THINGSET_CAN_TYPE_POS)

/* message priorities */
#define THINGSET_CAN_PRIO_POS       (26U)
#define THINGSET_CAN_PRIO_MASK      (0x7 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_SET(prio) ((uint32_t)prio << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_GET(id)   (((uint32_t)id & THINGSET_CAN_PRIO_MASK) >> THINGSET_CAN_PRIO_POS)

#define THINGSET_CAN_PRIO_CONTROL_EMERGENCY (0x0 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_CONTROL_HIGH      (0x2 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_CONTROL_LOW       (0x3 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_NETWORK_MGMT      (0x4 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_REPORT_HIGH       (0x5 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_CHANNEL           (0x6 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_REPORT_LOW        (0x7 << THINGSET_CAN_PRIO_POS)

/* below macros return true if the CAN ID matches the specified message type */
#define THINGSET_CAN_CONTROL(id) \
    (((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_CONTROL) && THINGSET_CAN_PRIO_GET(id) < 4)
#define THINGSET_CAN_REPORT(id) \
    (((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_REPORT) && THINGSET_CAN_PRIO_GET(id) >= 4)
#define THINGSET_CAN_PACKETIZED_REPORT(id) \
    (((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_PACKETIZED_REPORT) \
     && THINGSET_CAN_PRIO_GET(id) >= 4)
#define THINGSET_CAN_CHANNEL(id) ((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_CHANNEL)

/**
 * Callback typedef for received ThingSet report via CAN
 *
 * @param data_id ThingSet data object ID
 * @param value Buffer containing the CBOR raw data of the value
 * @param value_len Length of the value in the buffer
 * @param source_addr Node address the report was received from
 */
typedef void (*thingset_can_report_rx_callback_t)(uint16_t data_id, const uint8_t *value,
                                                  size_t value_len, uint8_t source_addr);
#ifdef CONFIG_ISOTP_FAST
typedef void (*thingset_can_response_callback_t)(uint8_t *data, size_t len, int result,
                                                 uint8_t sender_id, void *arg);

struct thingset_can_request_response
{
    struct k_sem sem;
    struct k_timer timer;
    isotp_fast_msg_id sender_addr;
    thingset_can_response_callback_t callback;
    void *cb_arg;
};
#endif /* CONFIG_ISOTP_FAST */

/**
 * ThingSet CAN context storing all information required for one instance.
 */
struct thingset_can
{
    const struct device *dev;
    struct k_work_delayable reporting_work;
    struct k_work_delayable addr_claim_work;
#ifdef CONFIG_ISOTP_FAST
    struct isotp_fast_ctx ctx;
#else
    struct isotp_recv_ctx recv_ctx;
    struct isotp_send_ctx send_ctx;
    struct isotp_msg_id rx_addr;
    struct isotp_msg_id tx_addr;
#endif
    struct k_event events;
#ifdef CONFIG_ISOTP_FAST
    struct thingset_can_request_response request_response;
#endif
    uint8_t rx_buffer[CONFIG_THINGSET_CAN_RX_BUF_SIZE];
    thingset_can_report_rx_callback_t report_rx_cb;
    int64_t next_pub_time;
    uint8_t node_addr;
};

#ifdef CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES

/**
 * Wait for incoming ThingSet message (usually requests)
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_buf Buffer to store the message.
 * @param rx_buf_size Size of the buffer to store the message.
 * @param source_addr Pointer to store the node address the data was received from.
 * @param timeout Timeout to wait for a message from the node.
 *
 * @returns length of message or negative errno in case of error
 */
int thingset_can_receive_inst(struct thingset_can *ts_can, uint8_t *rx_buf, size_t rx_buf_size,
                              uint8_t *source_addr, k_timeout_t timeout);

#ifdef CONFIG_ISOTP_FAST
/**
 * Send ThingSet message to other node
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 * @param rsp_callback If a response is expected, this callback will be invoked,
 * either when it arrives or if a timeout or some other error occurs.
 * @param callback_arg User data for the callback.
 * @param rsp_timeout Timeout to wait for a response.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr, thingset_can_response_callback_t rsp_callback,
                           void *callback_arg, k_timeout_t timeout);
#else
/**
 * Send ThingSet message to other node
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr);

/**
 * Process incoming ThingSet requests
 *
 * This function waits for incoming ThingSet requests, processes the request and sends the response
 * back to the node.
 *
 * The function returns after each sent response or after the timeout, so it must be called in a
 * continuous loop from a thread to keep listening to requests.
 *
 * A short timeout can be used to process multiple instances consecutively from the same thread.
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param timeout Timeout to wait for a message from the node.
 *
 * @retval 0 for success
 * @retval -EAGAIN in case of timeout
 */
int thingset_can_process_inst(struct thingset_can *ts_can, k_timeout_t timeout);
#endif /* CONFIG_ISOTP_FAST */

/**
 * Set callback for received reports from other nodes
 *
 * The callback can be used to subscribe to reports from other nodes, e.g. for control purposes.
 *
 * If not set, reports from other nodes are ignored on the bus.
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_cb Callback function.
 */
int thingset_can_set_report_rx_callback_inst(struct thingset_can *ts_can,
                                             thingset_can_report_rx_callback_t rx_cb);

/**
 * Initialize a ThingSet CAN instance
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param can_dev Pointer to the CAN device that should be used.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_init_inst(struct thingset_can *ts_can, const struct device *can_dev);

#else /* !CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES */

#ifdef CONFIG_ISOTP_FAST
/**
 * Send ThingSet message to other node
 *
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr,
                      thingset_can_response_callback_t rsp_callback, void *callback_arg,
                      k_timeout_t timeout);
#else
/**
 * Send ThingSet message to other node
 *
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr);
#endif /* CONFIG_ISOTP_FAST */

/**
 * Set callback for received reports from other nodes
 *
 * The callback can be used to subscribe to reports from other nodes, e.g. for control purposes.
 *
 * If not set, reports from other nodes are ignored on the bus.
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_cb Callback function.
 */
int thingset_can_set_report_rx_callback(thingset_can_report_rx_callback_t rx_cb);

/**
 * Get ThingSet CAN instance
 *
 * @returns Pointer to internal ThingSet CAN instance
 */
struct thingset_can *thingset_can_get_inst();

#endif /* CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES */

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_CAN_H_ */
