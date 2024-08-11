/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_CAN_H_
#define THINGSET_CAN_H_

#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>

#include "canbus/isotp_fast.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ThingSet addressing in 29-bit CAN ID
 *
 * Request/response messages using ISO-TP (bus forwarding scheme):
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
 * Request/response messages using ISO-TP (bridge forwarding scheme):
 *
 *    28      26 25 24 23      16 15            8 7             0
 *   +----------+-----+----------+---------------+---------------+
 *   | Priority | 0x0 |  bridge  |  target addr  |  source addr  |
 *   +----------+-----+----------+---------------+---------------+
 *
 *   Priority: 6
 *
 *   bridge: Bridge number for message forwarding (0x00 for local communication)
 *
 * Multi-frame reports:
 *
 *    28      26 25 24 23 20 19     16 15  14 13     12 11   8 7           0
 *   +----------+-----+-----+---------+------+---------+------+-------------+
 *   | Priority | 0x1 | res | src bus | msg# | MF type | seq# | source addr |
 *   +----------+-----+-----+---------+------+---------+------+-------------+
 *
 *   Priority: 5 or 7
 *   msg#: Wrapping message counter from 0 to 3
 *   MF type: 0: first frame, 1: consecutive frame, 2: last frame, 3: single frame
 *   seq#: Wrapping sequence counter from 0 to 15
 *   src bus and res: Either source bus or bridge number
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

/* message number, type and sequence number for multi-frame reports */
#define THINGSET_CAN_SEQ_NO_POS  (8U)
#define THINGSET_CAN_SEQ_NO_MASK (0xF << THINGSET_CAN_SEQ_NO_POS)
#define THINGSET_CAN_SEQ_NO_SET(no) \
    (((uint32_t)no << THINGSET_CAN_SEQ_NO_POS) & THINGSET_CAN_SEQ_NO_MASK)
#define THINGSET_CAN_SEQ_NO_GET(id) \
    (((uint32_t)id & THINGSET_CAN_SEQ_NO_MASK) >> THINGSET_CAN_SEQ_NO_POS)
#define THINGSET_CAN_MF_TYPE_POS    (12U)
#define THINGSET_CAN_MF_TYPE_MASK   (0x3 << THINGSET_CAN_MF_TYPE_POS)
#define THINGSET_CAN_MF_TYPE_FIRST  (0U << THINGSET_CAN_MF_TYPE_POS)
#define THINGSET_CAN_MF_TYPE_CONSEC (1U << THINGSET_CAN_MF_TYPE_POS)
#define THINGSET_CAN_MF_TYPE_LAST   (2U << THINGSET_CAN_MF_TYPE_POS)
#define THINGSET_CAN_MF_TYPE_SINGLE (3U << THINGSET_CAN_MF_TYPE_POS)
#define THINGSET_CAN_MSG_NO_POS     (14U)
#define THINGSET_CAN_MSG_NO_MASK    (0x3 << THINGSET_CAN_MSG_NO_POS)
#define THINGSET_CAN_MSG_NO_SET(no) \
    (((uint32_t)no << THINGSET_CAN_MSG_NO_POS) & THINGSET_CAN_MSG_NO_MASK)
#define THINGSET_CAN_MSG_NO_GET(id) \
    (((uint32_t)id & THINGSET_CAN_MSG_NO_MASK) >> THINGSET_CAN_MSG_NO_POS)

/* bus numbers for request/response messages and multi-frame reports */
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

/* bridge numbers for request/response messages and multi-frame reports */
#define THINGSET_CAN_BRIDGE_POS  (16U)
#define THINGSET_CAN_BRIDGE_MASK (0xFF << THINGSET_CAN_BRIDGE_POS)
#define THINGSET_CAN_BRIDGE_SET(id) \
    (((uint32_t)id << THINGSET_CAN_BRIDGE_POS) & THINGSET_CAN_BRIDGE_MASK)
#define THINGSET_CAN_BRIDGE_GET(id) \
    (((uint32_t)id & THINGSET_CAN_BRIDGE_MASK) >> THINGSET_CAN_BRIDGE_POS)
#define THINGSET_CAN_BRIDGE_LOCAL (0x00)

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
 * Callback typedef for received address claim frames from other nodes
 *
 * @param eui64 The EUI-64 of the node used for the address claim message.
 * @param source_addr Node address the address claim was received from
 */
typedef void (*thingset_can_addr_claim_rx_callback_t)(const uint8_t eui64[8], uint8_t source_addr);

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
 * @param source_addr Node address the item was received from
 */
typedef void (*thingset_can_item_rx_callback_t)(uint16_t data_id, const uint8_t *value,
                                                size_t value_len, uint8_t source_addr);

/**
 * Callback typedef for received responses via CAN ISO-TP
 *
 * @param data Buffer containing the ThingSet response or NULL in case of error.
 * @param length Length of the data in the buffer
 * @param send_err 0 for success or negative errno indicating a send error.
 * @param recv_err 0 for success or negative errno indicating a receive error.
 * @param source_addr Node address the response was received from
 * @param arg User-data passed to the callback
 */
typedef void (*thingset_can_reqresp_callback_t)(uint8_t *data, size_t len, int send_err,
                                                int recv_err, uint8_t source_addr, void *arg);

struct thingset_can_request_response
{
    struct k_sem sem;
    struct k_timer timer;
    uint32_t can_id;
    thingset_can_reqresp_callback_t callback;
    void *cb_arg;
};

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
    thingset_can_addr_claim_rx_callback_t addr_claim_callback;
    struct isotp_fast_ctx ctx;
    struct k_sem report_tx_sem;
    struct k_event events;
    struct thingset_can_request_response request_response;
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
    struct k_timer timeout_timer;
    uint8_t node_addr;
    /** bus or bridge number */
    uint8_t route;
    uint8_t msg_no;
};

#ifdef CONFIG_THINGSET_CAN_MULTIPLE_INSTANCES

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

/**
 * Send ThingSet message to other node
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param tx_buf Buffer containing the message.
 * @param tx_len Length of the message.
 * @param target_addr Target node address (8-bit value) to send the message to.
 * @param route Target bus/bridge number to send the message to.
 * @param callback This callback will be invoked when a response is received or an error during
 *                 sending or receiving occurred. Set to NULL if no response is expected.
 * @param callback_arg User data for the callback.
 * @param timeout Timeout to wait for a response.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send_inst(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                           uint8_t target_addr, uint8_t route,
                           thingset_can_reqresp_callback_t callback, void *callback_arg,
                           k_timeout_t timeout);

/**
 * Set callback for received address claim frames from other nodes
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param cb Callback function.
 */
void thingset_can_set_addr_claim_rx_callback_inst(struct thingset_can *ts_can,
                                                  thingset_can_addr_claim_rx_callback_t cb);

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
 * @param bus_number Assigned bus number of this CAN device (ignored if bridge routing is used)
 * @param timeout Initialisation timeout. Set to K_FOREVER for no timeout.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_init_inst(struct thingset_can *ts_can, const struct device *can_dev,
                           uint8_t bus_number, k_timeout_t timeout);

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

/**
 * Send ThingSet message to other node
 *
 * See thingset_can_send_inst() for function parameters.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send(uint8_t *tx_buf, size_t tx_len, uint8_t target_addr, uint8_t route,
                      thingset_can_reqresp_callback_t callback, void *callback_arg,
                      k_timeout_t timeout);

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
