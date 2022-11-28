/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_CAN_H_
#define THINGSET_CAN_H_

#include <zephyr/canbus/isotp.h>
#include <zephyr/device.h>

/**
 * ThingSet CAN context storing all information required for one instance.
 */
struct thingset_can
{
    const struct device *dev;
    struct k_work_delayable pub_work;
    struct isotp_recv_ctx recv_ctx;
    struct isotp_send_ctx send_ctx;
    struct isotp_msg_id rx_addr;
    struct isotp_msg_id tx_addr;
    struct k_event events;
    int64_t next_pub_time;
    uint8_t node_addr;
    bool pub_enable;
};

/**
 * Wait for incoming ThingSet messages
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param rx_buf Buffer to store the response from the node.
 * @param rx_buf_size Size of the buffer to store the response.
 * @param source_addr Pointer to store the node address the data was received from.
 * @param timeout Timeout to wait for a response from the node.
 *
 * @returns length of response or negative errno in case of error
 */
int thingset_can_receive(struct thingset_can *ts_can, uint8_t *rx_buf, size_t rx_buf_size,
                         uint8_t *source_addr, k_timeout_t timeout);

/**
 * Send ThingSet messages to other node
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param tx_buf Buffer containing the request.
 * @param tx_len Length of the request.
 * @param target_addr Target node address (8-bit value) to send the data to.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_send(struct thingset_can *ts_can, uint8_t *tx_buf, size_t tx_len,
                      uint8_t target_addr);

/**
 * Automatically process incoming ThingSet requests
 *
 * This function waits for incoming ThingSet requests, processes the request and sends the response
 * back to the node.
 *
 * The function returns after each sent response, so it must be called in a continuous loop from a
 * thread to keep listening.
 *
 * @param ts_can Pointer to the thingset_can context.
 */
void thingset_can_process(struct thingset_can *ts_can);

/**
 * Initialize a ThingSet CAN instance
 *
 * @param ts_can Pointer to the thingset_can context.
 * @param can_dev Pointer to the CAN device that should be used.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_can_init(struct thingset_can *ts_can, const struct device *can_dev);

#endif /* THINGSET_CAN_H_ */
