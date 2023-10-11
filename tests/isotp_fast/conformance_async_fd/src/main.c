/*
 * Copyright (c) 2023 Brill Power
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define ISOTP_FAST_CONFORMANCE_TEST_SUITE isotp_fast_conformance_async_fd
#include "../../conformance_async/src/async_recv.h"

#include "../../conformance/src/main.c"

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_sf_length)
{
    int ret;
    struct frame_desired des_frame;

    des_frame.data[0] = (SF_PCI_TYPE << PCI_TYPE_POS) | 7;
    memcpy(&des_frame.data[1], random_data, 7);
    des_frame.length = 8;

    /* mask to allow any priority and source address (SA) */
    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    ret = isotp_fast_send(&ctx, random_data, 7, rx_node_id, INT_TO_POINTER(ISOTP_N_OK));
    zassert_equal(ret, 0, "Send returned %d", ret);

    check_frame_series(&des_frame, 1, &frame_msgq);

    des_frame.data[0] = (SF_PCI_TYPE << PCI_TYPE_POS);
    des_frame.data[1] = 9;
    memcpy(&des_frame.data[2], random_data, DATA_SIZE_SF);
    des_frame.length = 9;

    ret = isotp_fast_send(&ctx, random_data, 9, rx_node_id, INT_TO_POINTER(ISOTP_N_OK));
    zassert_equal(ret, 0, "Send returned %d", ret);

    check_frame_series(&des_frame, 1, &frame_msgq);
}
