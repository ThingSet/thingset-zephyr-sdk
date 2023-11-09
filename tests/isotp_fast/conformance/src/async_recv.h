/*
 * Copyright (c) 2023 Brill Power
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "main.h"

struct recv_msg
{
    uint8_t data[CAN_MAX_DLEN];
    int16_t len;
    int rem_len;
};

K_MSGQ_DEFINE(recv_msgq, sizeof(struct recv_msg), DIV_ROUND_UP(DATA_SEND_LENGTH, DATA_SIZE_CF), 2);
int8_t recv_last_error;

static int blocking_recv(uint8_t *buf, size_t size, k_timeout_t timeout)
{
    int ret;
    struct recv_msg msg;
    int rx_len = 0;
    while ((ret = k_msgq_get(&recv_msgq, &msg, timeout)) == 0) {
        if (recv_last_error != 0) {
            ret = recv_last_error;
            recv_last_error = 0;
            return ret;
        }
        if (msg.len < 0) {
            /* an error has occurred */
            // printk("Error %d occurred", msg.len);
            return msg.len;
        }
        int cp_len = MIN(msg.len, size - rx_len);
        memcpy(buf, &msg.data, cp_len);
        rx_len += cp_len;
        buf += cp_len;
        if (msg.rem_len > (size - rx_len)) {
            /* recv buffer will probably overflow on next call; hand back to user code */
            break;
        }
        if (msg.rem_len == 0) {
            /* msg is complete */
            break;
        }
    }
    if (recv_last_error != 0) {
        ret = recv_last_error;
        recv_last_error = 0;
        return ret;
    }
    if (ret == -EAGAIN) {
        return ISOTP_RECV_TIMEOUT;
    }
    return rx_len;
}

void isotp_fast_recv_handler(struct net_buf *buffer, int rem_len, isotp_fast_can_id rx_can_id,
                             void *arg)
{
    struct recv_msg msg = {
        .len = buffer->len,
        .rem_len = rem_len,
    };
    memcpy(&msg.data, buffer->data, MIN(sizeof(msg.data), buffer->len));
    // printk("< [%x] [%02d] ", rx_can_id, buffer->len);
    // print_hex(&msg.data[0], msg.len);
    // printk("[%d]\n", rem_len);
    k_msgq_put(&recv_msgq, &msg, K_NO_WAIT);
}

void isotp_fast_recv_error_handler(int8_t error, isotp_fast_can_id rx_can_id, void *arg)
{
    // printk("Error %d received\n", error);
    recv_last_error = error;
    k_msgq_purge(&recv_msgq);
}
