/*
 * Copyright (c) 2019 Alexander Wachter
 * Copyright (c) 2023 Brill Power
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "random_data.h"
#include <strings.h>
#include <thingset/isotp_fast.h>
#include <zephyr/drivers/can.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define PCI_TYPE_POS 4
#ifdef CONFIG_CAN_FD_MODE
#define SF_LEN_BYTE 2 /* need extra byte to store length > 16 bytes */
#else
#define SF_LEN_BYTE 1
#endif
#define DATA_SIZE_SF          (CAN_MAX_DLEN - SF_LEN_BYTE)
#define DATA_SIZE_CF          (CAN_MAX_DLEN - 1)
#define DATA_SIZE_SF_EXT      (CAN_MAX_DLEN - 2)
#define DATA_SIZE_FF          (CAN_MAX_DLEN - 2)
#define CAN_DL                CAN_MAX_DLEN
#define DATA_SEND_LENGTH      272
#define SF_PCI_TYPE           0
#define SF_PCI_BYTE_1         ((SF_PCI_TYPE << PCI_TYPE_POS) | DATA_SIZE_SF)
#define SF_PCI_BYTE_2_EXT     ((SF_PCI_TYPE << PCI_TYPE_POS) | DATA_SIZE_SF_EXT)
#define SF_PCI_BYTE_LEN_8     ((SF_PCI_TYPE << PCI_TYPE_POS) | (DATA_SIZE_SF + 1))
#define EXT_ADDR              5
#define FF_PCI_TYPE           1
#define FF_PCI_BYTE_1(dl)     ((FF_PCI_TYPE << PCI_TYPE_POS) | ((dl) >> 8))
#define FF_PCI_BYTE_2(dl)     ((dl)&0xFF)
#define FC_PCI_TYPE           3
#define FC_PCI_CTS            0
#define FC_PCI_WAIT           1
#define FC_PCI_OVFLW          2
#define FC_PCI_BYTE_1(FS)     ((FC_PCI_TYPE << PCI_TYPE_POS) | (FS))
#define FC_PCI_BYTE_2(BS)     (BS)
#define FC_PCI_BYTE_3(ST_MIN) (ST_MIN)
#define CF_PCI_TYPE           2
#define CF_PCI_BYTE_1         (CF_PCI_TYPE << PCI_TYPE_POS)
#define STMIN_VAL_1           5
#define STMIN_VAL_2           50
#define STMIN_UPPER_TOLERANCE 5

#if defined(CONFIG_ISOTP_ENABLE_TX_PADDING) || defined(CONFIG_ISOTP_ENABLE_TX_PADDING)
#define DATA_SIZE_FC CAN_DL
#else
#define DATA_SIZE_FC 3
#endif

#define BS_TIMEOUT_UPPER_MS 1100
#define BS_TIMEOUT_LOWER_MS 1000

struct frame_desired
{
    uint8_t data[CAN_MAX_DLEN];
    uint8_t length;
};

struct frame_desired des_frames[DIV_ROUND_UP((DATA_SEND_LENGTH - DATA_SIZE_FF), DATA_SIZE_CF)];

const struct isotp_fast_opts fc_opts = {
    .bs = 8,
    .stmin = 0,
#ifdef CONFIG_CAN_FD_MODE
    .flags = ISOTP_MSG_FDF,
#else
    .flags = 0
#endif
};

const isotp_fast_can_id rx_can_id = 0x18DA0201;
const isotp_fast_can_id tx_can_id = 0x18DA0102;

const isotp_fast_node_id rx_node_id = 0x01;
const isotp_fast_node_id tx_node_id = 0x02;

const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
struct isotp_fast_ctx ctx;
uint8_t data_buf[128];
CAN_MSGQ_DEFINE(frame_msgq, 10);
struct k_sem send_compl_sem;
int filter_id;

static void print_hex(const uint8_t *ptr, size_t len)
{
    while (len--) {
        printk("%02x ", *ptr++);
    }
}
