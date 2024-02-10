/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_CAN_H_
#define THINGSET_CAN_H_

#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>

#ifdef CONFIG_ISOTP_FAST
#include "isotp_fast.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ThingSet addressing in 29-bit CAN ID
 *
 * Request/response messages using ISO-TP:
 *
 *    28      26 25 24 23     20 19     16 15            8 7             0
 *   +----------+-----+---------+---------+---------------+---------------+
 *   | Priority | 0x0 | tgt bus | src bus |  target addr  |  source addr  |
 *   +----------+-----+---------+---------+---------------+---------------+
 *
 *   Priority: 6
 *
 *   tgt bus: Bus number of the target node (default for single bus systems is 0x0)
 *   src bus: Bus number of the source node (default for single bus systems is 0x0)
 *
 * Multi-frame reports:
 *
 *    28      26 25 24 23 20 19     16 15  13   12  11   8 7           0
 *   +----------+-----+-----+---------+------+-----+------+-------------+
 *   | Priority | 0x1 | res | src bus | msg# | end | seq# | source addr |
 *   +----------+-----+-----+---------+------+-----+------+-------------+
 *
 *   Priority: 5 or 7
 *   msg#: Wrapping message counter from 0 to 7
 *   end: End of message flag
 *   seq#: Wrapping sequence counter from 0 to 15
 *
 * Single-frame reports:
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

#define THINGSET_CAN_ADDR_MIN       (0x01)
#define THINGSET_CAN_ADDR_MAX       (0xFD)
#define THINGSET_CAN_ADDR_ANONYMOUS (0xFE)
#define THINGSET_CAN_ADDR_BROADCAST (0xFF)

/* data IDs for single-frame reports */
#define THINGSET_CAN_DATA_ID_POS  (8U)
#define THINGSET_CAN_DATA_ID_MASK (0xFFFF << THINGSET_CAN_DATA_ID_POS)
#define THINGSET_CAN_DATA_ID_SET(id) \
    (((uint32_t)id << THINGSET_CAN_DATA_ID_POS) & THINGSET_CAN_DATA_ID_MASK)
#define THINGSET_CAN_DATA_ID_GET(id) \
    (((uint32_t)id & THINGSET_CAN_DATA_ID_MASK) >> THINGSET_CAN_DATA_ID_POS)

/* message number, end flag and sequence number for multi-frame reports */
#define THINGSET_CAN_SEQ_NO_POS  (8U)
#define THINGSET_CAN_SEQ_NO_MASK (0xF << THINGSET_CAN_SEQ_NO_POS)
#define THINGSET_CAN_SEQ_NO_SET(no) \
    (((uint32_t)no << THINGSET_CAN_SEQ_NO_POS) & THINGSET_CAN_SEQ_NO_MASK)
#define THINGSET_CAN_SEQ_NO_GET(id) \
    (((uint32_t)id & THINGSET_CAN_SEQ_NO_MASK) >> THINGSET_CAN_SEQ_NO_POS)
#define THINGSET_CAN_END_FLAG_POS  (12U)
#define THINGSET_CAN_END_FLAG_MASK (0x1 << THINGSET_CAN_END_FLAG_POS)
#define THINGSET_CAN_END_FLAG_SET(val) \
    (((uint32_t)val << THINGSET_CAN_END_FLAG_POS) & THINGSET_CAN_END_FLAG_MASK)
#define THINGSET_CAN_END_FLAG_GET(id) \
    (((uint32_t)id & THINGSET_CAN_END_FLAG_MASK) >> THINGSET_CAN_END_FLAG_POS)
#define THINGSET_CAN_MSG_NO_POS  (13U)
#define THINGSET_CAN_MSG_NO_MASK (0x7 << THINGSET_CAN_MSG_NO_POS)
#define THINGSET_CAN_MSG_NO_SET(no) \
    (((uint32_t)no << THINGSET_CAN_MSG_NO_POS) & THINGSET_CAN_MSG_NO_MASK)
#define THINGSET_CAN_MSG_NO_GET(id) \
    (((uint32_t)id & THINGSET_CAN_MSG_NO_MASK) >> THINGSET_CAN_MSG_NO_POS)

/* bus numbers for request/response messages */
#define THINGSET_CAN_SOURCE_BUS_POS  (16U)
#define THINGSET_CAN_SOURCE_BUS_MASK (0xF << THINGSET_CAN_SOURCE_BUS_POS)
#define THINGSET_CAN_SOURCE_BUS_SET(id) \
    (((uint32_t)id << THINGSET_CAN_SOURCE_BUS_POS) & THINGSET_CAN_SOURCE_BUS_MASK)
#define THINGSET_CAN_SOURCE_BUS_GET(id) \
    (((uint32_t)id & THINGSET_CAN_SOURCE_BUS_MASK) >> THINGSET_CAN_SOURCE_BUS_POS)
#define THINGSET_CAN_SOURCE_BUS_DEFAULT (0x0)
#define THINGSET_CAN_TARGET_BUS_POS     (20U)
#define THINGSET_CAN_TARGET_BUS_MASK    (0xF << THINGSET_CAN_TARGET_BUS_POS)
#define THINGSET_CAN_TARGET_BUS_SET(id) \
    (((uint32_t)id << THINGSET_CAN_TARGET_BUS_POS) & THINGSET_CAN_TARGET_BUS_MASK)
#define THINGSET_CAN_TARGET_BUS_GET(id) \
    (((uint32_t)id & THINGSET_CAN_TARGET_BUS_MASK) >> THINGSET_CAN_TARGET_BUS_POS)
#define THINGSET_CAN_TARGET_BUS_DEFAULT (0x0)

/* random number for address discovery messages */
#define THINGSET_CAN_RAND_POS     (16U)
#define THINGSET_CAN_RAND_MASK    (0xFF << THINGSET_CAN_RAND_POS)
#define THINGSET_CAN_RAND_SET(id) (((uint32_t)id << THINGSET_CAN_RAND_POS) & THINGSET_CAN_RAND_MASK)
#define THINGSET_CAN_RAND_GET(id) (((uint32_t)id & THINGSET_CAN_RAND_MASK) >> THINGSET_CAN_RAND_POS)

/* message types */
#define THINGSET_CAN_TYPE_POS  (24U)
#define THINGSET_CAN_TYPE_MASK (0x3 << THINGSET_CAN_TYPE_POS)

#define THINGSET_CAN_TYPE_REQRESP   (0x0 << THINGSET_CAN_TYPE_POS)
#define THINGSET_CAN_TYPE_MF_REPORT (0x1 << THINGSET_CAN_TYPE_POS)
#define THINGSET_CAN_TYPE_SF_REPORT (0x2 << THINGSET_CAN_TYPE_POS)
#define THINGSET_CAN_TYPE_NETWORK   (0x3 << THINGSET_CAN_TYPE_POS)

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
#define THINGSET_CAN_PRIO_REQRESP           (0x6 << THINGSET_CAN_PRIO_POS)
#define THINGSET_CAN_PRIO_REPORT_LOW        (0x7 << THINGSET_CAN_PRIO_POS)

/* below macros return true if the CAN ID matches the specified message type */
#define THINGSET_CAN_CONTROL(id) \
    (((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_CONTROL) && THINGSET_CAN_PRIO_GET(id) < 4)
#define THINGSET_CAN_SF_REPORT(id) \
    (((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_SF_REPORT) \
     && THINGSET_CAN_PRIO_GET(id) >= 4)
#define THINGSET_CAN_MF_REPORT(id) \
    (((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_MF_REPORT) \
     && THINGSET_CAN_PRIO_GET(id) >= 4)
#define THINGSET_CAN_REQRESP(id) ((id & THINGSET_CAN_TYPE_MASK) == THINGSET_CAN_TYPE_REQRESP)

/**
 * Callback typedef for received multi-frame reports (type 0x1) via CAN
 *
 * @param report_buf Pointer to the buffer containing the received report (text or binary format)
 * @param report_len Length of the report in the buffer
 * @param source_addr Node address the report was received from
 */
typedef void (*thingset_can_report_rx_callback_t)(const uint8_t *report_buf, size_t report_len,
                                                  uint8_t source_addr);

/**
 * Callback typedef for received single-frame reports (type 0x2) via CAN
 *
 * @param data_id ThingSet data object ID
 * @param value Buffer containing the CBOR raw data of the value
 * @param value_len Length of the value in the buffer
 * @param source_addr Node address the control message was received from
 */
typedef void (*thingset_can_item_rx_callback_t)(uint16_t data_id, const uint8_t *value,
                                                size_t value_len, uint8_t source_addr);

#ifdef CONFIG_ISOTP_FAST
typedef void (*thingset_can_response_callback_t)(uint8_t *data, size_t len, int result,
                                                 uint8_t sender_id, void *arg);

struct thingset_can_request_response
{
    struct k_sem sem;
    struct k_timer timer;
    uint32_t can_id;
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
    struct k_work_delayable live_reporting_work;
#ifdef CONFIG_THINGSET_CAN_CONTROL_REPORTING
    struct k_work_delayable control_reporting_work;
#endif
    struct k_work_delayable addr_claim_work;
#ifdef CONFIG_ISOTP_FAST
    struct isotp_fast_ctx ctx;
#else
    struct isotp_recv_ctx recv_ctx;
    struct isotp_send_ctx send_ctx;
    struct isotp_msg_id rx_addr;
    struct isotp_msg_id tx_addr;
#endif
    struct k_sem report_tx_sem;
    struct k_event events;
#ifdef CONFIG_ISOTP_FAST
    struct thingset_can_request_response request_response;
#endif
    uint8_t rx_buffer[CONFIG_THINGSET_CAN_RX_BUF_SIZE];
#ifdef CONFIG_THINGSET_CAN_REPORT_RX
    thingset_can_report_rx_callback_t report_rx_cb;
#endif
#ifdef CONFIG_THINGSET_CAN_ITEM_RX
    thingset_can_item_rx_callback_t item_rx_cb;
#endif
    int64_t next_live_report_time;
#ifdef CONFIG_THINGSET_CAN_CONTROL_REPORTING
    int64_t next_control_report_time;
#endif
    uint8_t node_addr;
    uint8_t bus_number : 4;
    uint8_t msg_no;
};

#ifdef CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES

/**
 * Wait for incoming ThingSet message (usually requests)
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_buf Buffer to store the message.
 * @param rx_buf_size Size of the buffer to store the message.
 * @param source_addr Pointer to store the node address the data was received from.
 * @param source_bus Pointer to store the bus number the data was received from.
 * @param timeout Timeout to wait for a message from the node.
 *
 * @returns length of message or negative errno in case of error
 */
int thingset_can_receive_inst(struct thingset_can *ts_can, uint8_t *rx_buf, size_t rx_buf_size,
                              uint8_t *source_addr, uint8_t *source_bus, k_timeout_t timeout);

/**
 * Send ThingSet report to the CAN bus.
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param path Path of subset/group/record to be published
 * @param format Protocol data format to be used (text, binary with IDs or binary with names)
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_report_inst(struct thingset_can *ts_can, const char *path,
                                  enum thingset_data_format format);

#ifdef CONFIG_ISOTP_FAST
/**
 * Send ThingSet message to other node
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 * @param target_bus Target bus number (4-bit value) to send the message to.
 * @param rsp_callback If a response is expected, this callback will be invoked,
 *                     either when it arrives or if a timeout or some other error occurs.
 * @param callback_arg User data for the callback.
 * @param timeout Timeout to wait for a response.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr, uint8_t target_bus,
                           thingset_can_response_callback_t rsp_callback, void *callback_arg,
                           k_timeout_t timeout);
#else
/**
 * Send ThingSet message to other node
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 * @param target_bus Target bus number (4-bit value) to send the message to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr, uint8_t target_bus);

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

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
/**
 * Set callback for received reports from other nodes
 *
 * These messages use ThingSet CAN frame type 0x1 (multi-frame report).
 *
 * The callback can be used to subscribe to reports from other nodes.
 *
 * If not set, reports from other nodes are ignored on the bus.
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_cb Callback function.
 */
int thingset_can_set_report_rx_callback_inst(struct thingset_can *ts_can,
                                             thingset_can_report_rx_callback_t rx_cb);
#endif /* CONFIG_THINGSET_CAN_REPORT_RX */

#ifdef CONFIG_THINGSET_CAN_ITEM_RX
/**
 * Set callback for received data items from other nodes
 *
 * These messages use ThingSet CAN frame type 0x2 (single-frame report).
 *
 * If not set, single-frame reports from other nodes are ignored on the bus.
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_cb Callback function.
 */
int thingset_can_set_item_rx_callback_inst(struct thingset_can *ts_can,
                                           thingset_can_report_rx_callback_t rx_cb);
#endif /* CONFIG_THINGSET_CAN_ITEM_RX */

/**
 * Initialize a ThingSet CAN instance
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param can_dev Pointer to the CAN device that should be used.
 * @param bus_number Assigned bus number of this CAN device.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_init_inst(struct thingset_can *ts_can, const struct device *can_dev,
                           uint8_t bus_number);

#else /* !CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES */

/**
 * Send ThingSet report to the CAN bus.
 *
 * @param path Path of subset/group/record to be published
 * @param format Protocol data format to be used (text, binary with IDs or binary with names)
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_report(const char *path, enum thingset_data_format format);

#ifdef CONFIG_ISOTP_FAST
/**
 * Send ThingSet message to other node
 *
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 * @param target_bus Target bus number (4-bit value) to send the message to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr, uint8_t target_bus,
                      thingset_can_response_callback_t rsp_callback, void *callback_arg,
                      k_timeout_t timeout);
#else
/**
 * Send ThingSet message to other node
 *
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 * @param target_bus Target bus number (4-bit value) to send the message to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr, uint8_t target_bus);
#endif /* CONFIG_ISOTP_FAST */

#ifdef CONFIG_THINGSET_CAN_REPORT_RX
/**
 * Set callback for received reports from other nodes
 *
 * These messages use ThingSet CAN frame type 0x1 (multi-frame report).
 *
 * The callback can be used to subscribe to reports from other nodes.
 *
 * If not set, reports from other nodes are ignored on the bus.
 *
 * @param rx_cb Callback function.
 */
int thingset_can_set_report_rx_callback(thingset_can_report_rx_callback_t rx_cb);
#endif /* CONFIG_THINGSET_CAN_REPORT_RX */

#ifdef CONFIG_THINGSET_CAN_ITEM_RX
/**
 * Set callback for received data items from other nodes
 *
 * These messages use ThingSet CAN frame type 0x2 (single-frame report).
 *
 * If not set, single-frame reports from other nodes are ignored on the bus.
 *
 * @param rx_cb Callback function.
 */
int thingset_can_set_item_rx_callback(thingset_can_item_rx_callback_t rx_cb);
#endif /* CONFIG_THINGSET_CAN_ITEM_RX */

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
