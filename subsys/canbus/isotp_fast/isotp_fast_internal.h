/*
 * Copyright (c) 2023 Brill Power
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "isotp_internal.h"
#include <canbus/isotp_fast.h>
#include <zephyr/sys/slist.h>

#ifdef CONFIG_ISOTP_FAST_PER_FRAME_DISPATCH
#define ISOTP_FAST_RECEIVE_QUEUE
#endif
#ifdef CONFIG_ISOTP_FAST_BLOCKING_RECEIVE
#define ISOTP_FAST_RECEIVE_QUEUE
#endif

#ifdef CONFIG_CAN_FD_MODE
#define ISOTP_FAST_SF_LEN_BYTE 2
#else
#define ISOTP_FAST_SF_LEN_BYTE 1
#endif

#define ISOTP_FAST_MAX_LEN 4095

#define ISOTP_4BIT_SF_MAX_CAN_DL 8

/**
 * Internal send context. Used to manage the transmission of a single
 * message greater than 1 CAN frame in size.
 */
struct isotp_fast_send_ctx
{
    sys_snode_t node;               /**< linked list node in @ref isotp_send_ctx_list */
    struct isotp_fast_ctx *ctx;     /**< pointer to bound context */
    struct isotp_fast_addr tx_addr; /**< Address used on sent message frames */
    struct k_work work;
    struct k_timer timer;          /**< handles timeouts */
    struct k_sem sem;              /**< used to ensure CF frames are sent in order */
    const uint8_t *data;           /**< source message buffer */
    uint16_t rem_len : 12;         /**< length of buffer; max len 4095 */
    enum isotp_tx_state state : 8; /**< current state of context */
    int8_t error;
    void *cb_arg; /**< supplied to sent_callback */
    uint8_t wft;
    uint8_t bs;
    uint8_t sn : 4; /**< sequence number; overflows at 4 bits per spec */
    uint8_t backlog;
    uint8_t stmin;
};

/**
 * Internal receive context. Used to manage the receipt of a single
 * message over one or more CAN frames.
 */
struct isotp_fast_recv_ctx
{
    sys_snode_t node;               /**< linked list node in @ref isotp_recv_ctx_list */
    struct isotp_fast_ctx *ctx;     /**< pointer to bound context */
    struct isotp_fast_addr rx_addr; /**< Address on received frames */
    struct k_work work;
    struct k_timer timer;   /**< handles timeouts */
    struct net_buf *buffer; /**< head node of buffer */
    struct net_buf *frag;   /**< current fragment */
#ifdef ISOTP_FAST_RECEIVE_QUEUE
    struct k_msgq recv_queue;
    uint8_t recv_queue_pool[sizeof(struct net_buf *) * CONFIG_ISOTP_FAST_RX_MAX_PACKET_COUNT];
#endif
    uint16_t rem_len : 12;         /**< remaining length of incoming message */
    enum isotp_rx_state state : 8; /**< current state of context */
    int8_t error;
    uint8_t wft;
    uint8_t bs;
    uint8_t sn_expected : 4;
#ifdef ISOTP_FAST_RECEIVE_QUEUE
    bool pending;
#endif
};

#ifdef CONFIG_ISOTP_FAST_BLOCKING_RECEIVE
struct isotp_fast_recv_await_ctx
{
    sys_snode_t node;
    struct can_filter sender;
    struct k_sem sem;
    struct isotp_fast_recv_ctx *rctx;
};
#endif
