/*
 * Copyright (c) 2019 Alexander Wachter
 * Copyright (c) 2023 Brill Power
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * @addtogroup t_can
 * @{
 * @defgroup t_can_isotp test_can_isotp
 * @brief TestPurpose: verify correctness of the iso tp implementation
 * @details
 * - Test Steps
 *   -#
 * - Expected Results
 *   -#
 * @}
 */

void isotp_fast_sent_handler(int result, void *arg)
{
    int expected_err_nr = POINTER_TO_INT(arg);

    zassert_equal(result, expected_err_nr, "Unexpected error nr. expect: %d, got %d",
                  expected_err_nr, result);
    k_sem_give(&send_compl_sem);
}

static int check_data(const uint8_t *frame, const uint8_t *desired, size_t length)
{
    int ret;

    ret = memcmp(frame, desired, length);
    if (ret) {
        printk("desired bytes:\n");
        print_hex(desired, length);
        printk("\nreceived (%zu bytes):\n", length);
        print_hex(frame, length);
        printk("\n");
    }

    return ret;
}

static void send_sf(void)
{
    int ret;

    ret = isotp_fast_send(&ctx, random_data, DATA_SIZE_SF, rx_node_id, NULL);
    zassert_equal(ret, 0, "Send returned %d", ret);
}

static void get_sf(struct isotp_fast_ctx *recv_ctx, size_t data_size)
{
    int ret;

    memset(data_buf, 0, sizeof(data_buf));
    ret = blocking_recv(data_buf, sizeof(data_buf), K_MSEC(1000));
    zassert_equal(ret, data_size, "recv returned %d", ret);

    ret = check_data(data_buf, random_data, data_size);
    zassert_equal(ret, 0, "Data differ");
}

static void get_sf_ignore(struct isotp_fast_ctx *recv_ctx)
{
    int ret;

    ret = blocking_recv(data_buf, sizeof(data_buf), K_MSEC(200));
    zassert_equal(ret, ISOTP_RECV_TIMEOUT, "recv returned %d", ret);
}

static void send_test_data(const uint8_t *data, size_t len)
{
    int ret;

    ret = isotp_fast_send(&ctx, data, len, rx_node_id, INT_TO_POINTER(ISOTP_N_OK));
    zassert_equal(ret, 0, "Send returned %d", ret);
}

static void receive_test_data(struct isotp_fast_ctx *recv_ctx, const uint8_t *data, size_t len,
                              int32_t delay)
{
    size_t remaining_len = len;
    int ret, recv_len;
    const uint8_t *data_ptr = data;

    do {
        memset(data_buf, 0, sizeof(data_buf));
        recv_len = blocking_recv(data_buf, sizeof(data_buf), K_MSEC(1000));
        zassert_true(recv_len >= 0, "recv error: %d", recv_len);

        zassert_true(remaining_len >= recv_len, "More data than expected");
        ret = check_data(data_buf, data_ptr, recv_len);
        zassert_equal(ret, 0, "Data differ");
        data_ptr += recv_len;
        remaining_len -= recv_len;

        if (delay) {
            k_msleep(delay);
        }
    } while (remaining_len);

    ret = blocking_recv(data_buf, sizeof(data_buf), K_MSEC(50));
    zassert_equal(ret, ISOTP_RECV_TIMEOUT, "Expected timeout but got %d", ret);
}

static void send_frame_series(struct frame_desired *frames, size_t length, uint32_t id)
{
    int i, ret;
    struct can_frame frame = { .flags = (id > 0x7FF) ? CAN_FRAME_IDE : 0, .id = id };
#ifdef CONFIG_CAN_FD_MODE
    frame.flags |= CAN_FRAME_FDF;
#endif
    struct frame_desired *desired = frames;

    for (i = 0; i < length; i++) {
        frame.dlc = can_bytes_to_dlc(desired->length);
        memcpy(frame.data, desired->data, desired->length);
        // printk("> [%x] [%02d] ", frame.id, desired->length);
        // print_hex(frame.data, desired->length);
        // printk("\n");
        ret = can_send(can_dev, &frame, K_MSEC(500), NULL, NULL);
        zassert_equal(ret, 0, "Sending msg %d failed (error %d).", i, ret);
        desired++;
    }
}

static void check_frame_series(struct frame_desired *frames, size_t length, struct k_msgq *msgq)
{
    int i, ret;
    struct can_frame frame;
    struct frame_desired *desired = frames;

    for (i = 0; i < length; i++) {
        ret = k_msgq_get(msgq, &frame, K_MSEC(500));
        zassert_equal(ret, 0, "Timeout waiting for msg nr %d. ret: %d", i, ret);
        // printk("RECV: ");
        // print_hex(frame.data, can_dlc_to_bytes(frame.dlc));
        // printk("(%x) \n", frame.id);
        /* normalise the lengths here so they are comparable */
        zassert_equal(frame.dlc, can_bytes_to_dlc(desired->length),
                      "DLC of frame nr %d differ. Desired: %d, Got: %d", i,
                      can_bytes_to_dlc(desired->length), frame.dlc);

        ret = check_data(frame.data, desired->data, desired->length);
        zassert_equal(ret, 0, "Data differ");

        desired++;
    }
    ret = k_msgq_get(msgq, &frame, K_MSEC(200));
    zassert_equal(ret, -EAGAIN,
                  "Expected timeout, but received %d; %02x %02x %02x %02x %02x %02x %02x %02x", ret,
                  frame.data[0], frame.data[1], frame.data[2], frame.data[3], frame.data[4],
                  frame.data[5], frame.data[6], frame.data[7]);
}

static int add_rx_msgq(uint32_t id, uint32_t mask)
{
    int filter_id;
    struct can_filter filter = { .flags = CAN_FILTER_DATA | ((id > 0x7FF) ? CAN_FILTER_IDE : 0),
                                 .id = id,
                                 .mask = mask };
#ifdef CONFIG_CAN_FD_MODE
    filter.flags |= CAN_FILTER_FDF;
#endif

    filter_id = can_add_rx_filter_msgq(can_dev, &frame_msgq, &filter);
    zassert_not_equal(filter_id, -ENOSPC, "Filter full");
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    return filter_id;
}

static void prepare_cf_frames(struct frame_desired *frames, size_t frames_cnt, const uint8_t *data,
                              size_t data_len)
{
    int i;
    const uint8_t *data_ptr = data;
    size_t remaining_length = data_len;

    for (i = 0; i < frames_cnt && remaining_length; i++) {
        frames[i].data[0] = CF_PCI_BYTE_1 | ((i + 1) & 0x0F);
        frames[i].length = CAN_DL;
        memcpy(&des_frames[i].data[1], data_ptr, DATA_SIZE_CF);

        if (remaining_length < DATA_SIZE_CF) {
            frames[i].length = remaining_length + 1;
            remaining_length = 0;
        }

        remaining_length -= DATA_SIZE_CF;
        data_ptr += DATA_SIZE_CF;
    }
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_send_sf)
{
    struct frame_desired des_frame;

#ifdef CONFIG_CAN_FD_MODE
    des_frame.data[0] = (SF_PCI_TYPE << PCI_TYPE_POS);
    des_frame.data[1] = DATA_SIZE_SF;
#else
    des_frame.data[0] = SF_PCI_BYTE_1;
#endif
    memcpy(&des_frame.data[SF_LEN_BYTE], random_data, DATA_SIZE_SF);
    des_frame.length = CAN_MAX_DLEN;

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    send_sf();

    check_frame_series(&des_frame, 1, &frame_msgq);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_receive_sf)
{
    struct frame_desired single_frame;

#ifdef CONFIG_CAN_FD_MODE
    single_frame.data[0] = (SF_PCI_TYPE << PCI_TYPE_POS);
    single_frame.data[1] = DATA_SIZE_SF;
#else
    single_frame.data[0] = SF_PCI_BYTE_1;
#endif
    memcpy(&single_frame.data[SF_LEN_BYTE], random_data, DATA_SIZE_SF);
    single_frame.length = CAN_MAX_DLEN;

    send_frame_series(&single_frame, 1, rx_addr);

    get_sf(&ctx, DATA_SIZE_SF);

    single_frame.data[0] = SF_PCI_BYTE_LEN_8;
    send_frame_series(&single_frame, 1, rx_addr);

    get_sf_ignore(&ctx);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_send_sf_fixed)
{
    int ret;
    struct frame_desired des_frame;

#ifdef CONFIG_CAN_FD_MODE
    des_frame.data[0] = (SF_PCI_TYPE << PCI_TYPE_POS);
    des_frame.data[1] = DATA_SIZE_SF;
#else
    des_frame.data[0] = SF_PCI_BYTE_1;
#endif
    memcpy(&des_frame.data[SF_LEN_BYTE], random_data, DATA_SIZE_SF);
    des_frame.length = CAN_MAX_DLEN;

    /* mask to allow any priority and source address (SA) */
    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    ret = isotp_fast_send(&ctx, random_data, DATA_SIZE_SF, rx_node_id, INT_TO_POINTER(ISOTP_N_OK));
    zassert_equal(ret, 0, "Send returned %d", ret);

    check_frame_series(&des_frame, 1, &frame_msgq);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_receive_sf_fixed)
{
    struct frame_desired single_frame;

#ifdef CONFIG_CAN_FD_MODE
    single_frame.data[0] = (SF_PCI_TYPE << PCI_TYPE_POS);
    single_frame.data[1] = DATA_SIZE_SF;
#else
    single_frame.data[0] = SF_PCI_BYTE_1;
#endif
    memcpy(&single_frame.data[SF_LEN_BYTE], random_data, DATA_SIZE_SF);
    single_frame.length = CAN_MAX_DLEN;

    /* default source address */
    send_frame_series(&single_frame, 1, rx_addr);
    get_sf(&ctx, DATA_SIZE_SF);

    /* different source address */
    send_frame_series(&single_frame, 1, rx_addr | 0xFF);
    get_sf(&ctx, DATA_SIZE_SF);

    /* different priority */
    send_frame_series(&single_frame, 1, rx_addr | (7U << 26));
    get_sf(&ctx, DATA_SIZE_SF);

    /* different target address (should fail) */
    send_frame_series(&single_frame, 1, rx_addr | 0xFF00);
    get_sf_ignore(&ctx);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_send_data)
{
    struct frame_desired fc_frame, ff_frame;
    const uint8_t *data_ptr = random_data;
    size_t remaining_length = DATA_SEND_LENGTH;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], data_ptr, DATA_SIZE_FF);
    ff_frame.length = CAN_DL;
    data_ptr += DATA_SIZE_FF;
    remaining_length -= DATA_SIZE_FF;

    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_frame.data[1] = FC_PCI_BYTE_2(0);
    fc_frame.data[2] = FC_PCI_BYTE_3(0);
    fc_frame.length = DATA_SIZE_FC;

    prepare_cf_frames(des_frames, ARRAY_SIZE(des_frames), data_ptr, remaining_length);

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    send_test_data(random_data, DATA_SEND_LENGTH);

    check_frame_series(&ff_frame, 1, &frame_msgq);

    send_frame_series(&fc_frame, 1, rx_addr);

    check_frame_series(des_frames, ARRAY_SIZE(des_frames), &frame_msgq);
}

/* hiding this whole test to avoid compiler errors */
#ifndef CONFIG_CAN_FD_MODE
ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_send_data_blocks)
{
    const uint8_t *data_ptr = random_data;
    size_t remaining_length = DATA_SEND_LENGTH;
    struct frame_desired *data_frame_ptr = des_frames;
    int ret;
    struct can_frame dummy_frame;
    struct frame_desired fc_frame, ff_frame;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], data_ptr, DATA_SIZE_FF);
    ff_frame.length = DATA_SIZE_FF + 2;
    data_ptr += DATA_SIZE_FF;
    remaining_length -= DATA_SIZE_FF;

    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_frame.data[1] = FC_PCI_BYTE_2(fc_opts.bs);
    fc_frame.data[2] = FC_PCI_BYTE_3(0);
    fc_frame.length = DATA_SIZE_FC;

    prepare_cf_frames(des_frames, ARRAY_SIZE(des_frames), data_ptr, remaining_length);

    remaining_length = DATA_SEND_LENGTH;

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    send_test_data(random_data, DATA_SEND_LENGTH);

    check_frame_series(&ff_frame, 1, &frame_msgq);
    remaining_length -= DATA_SIZE_FF;

    send_frame_series(&fc_frame, 1, rx_addr);

    check_frame_series(data_frame_ptr, fc_opts.bs, &frame_msgq);
    data_frame_ptr += fc_opts.bs;
    remaining_length -= fc_opts.bs * DATA_SIZE_CF;
    ret = k_msgq_get(&frame_msgq, &dummy_frame, K_MSEC(50));
    zassert_equal(ret, -EAGAIN, "Expected timeout but got %d", ret);

    fc_frame.data[1] = FC_PCI_BYTE_2(2);
    send_frame_series(&fc_frame, 1, rx_addr);

    /* dynamic bs */
    check_frame_series(data_frame_ptr, 2, &frame_msgq);
    data_frame_ptr += 2;
    remaining_length -= 2 * DATA_SIZE_CF;
    ret = k_msgq_get(&frame_msgq, &dummy_frame, K_MSEC(50));
    zassert_equal(ret, -EAGAIN, "Expected timeout but got %d", ret);

    /* get the rest */
    fc_frame.data[1] = FC_PCI_BYTE_2(0);
    send_frame_series(&fc_frame, 1, rx_addr);

    check_frame_series(data_frame_ptr, DIV_ROUND_UP(remaining_length, DATA_SIZE_CF), &frame_msgq);
    ret = k_msgq_get(&frame_msgq, &dummy_frame, K_MSEC(50));
    zassert_equal(ret, -EAGAIN, "Expected timeout but got %d", ret);
}
#endif /* CONFIG_CAN_FD_MODE */

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_receive_data)
{
    const uint8_t *data_ptr = random_data;
    size_t remaining_length = DATA_SEND_LENGTH;
    struct frame_desired fc_frame, ff_frame;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], data_ptr, DATA_SIZE_FF);
    ff_frame.length = CAN_DL;
    data_ptr += DATA_SIZE_FF;
    remaining_length -= DATA_SIZE_FF;

    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_frame.data[1] = FC_PCI_BYTE_2(fc_opts.bs);
    fc_frame.data[2] = FC_PCI_BYTE_3(fc_opts.stmin);
    fc_frame.length = DATA_SIZE_FC;

    prepare_cf_frames(des_frames, ARRAY_SIZE(des_frames), data_ptr, remaining_length);

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);

    send_frame_series(&ff_frame, 1, rx_addr);

    check_frame_series(&fc_frame, 1, &frame_msgq);

    send_frame_series(des_frames, ARRAY_SIZE(des_frames), rx_addr);

    receive_test_data(&ctx, random_data, DATA_SEND_LENGTH, 0);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_receive_data_blocks)
{
    const uint8_t *data_ptr = random_data;
    size_t remaining_length = DATA_SEND_LENGTH;
    struct frame_desired *data_frame_ptr = des_frames;
    int ret;
    size_t remaining_frames;
    struct frame_desired fc_frame, ff_frame;

    struct can_frame dummy_frame;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], data_ptr, DATA_SIZE_FF);
    ff_frame.length = DATA_SIZE_FF + 2;
    data_ptr += DATA_SIZE_FF;
    remaining_length -= DATA_SIZE_FF;

    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_frame.data[1] = FC_PCI_BYTE_2(fc_opts.bs);
    fc_frame.data[2] = FC_PCI_BYTE_3(fc_opts.stmin);
    fc_frame.length = DATA_SIZE_FC;

    prepare_cf_frames(des_frames, ARRAY_SIZE(des_frames), data_ptr, remaining_length);

    remaining_frames = DIV_ROUND_UP(remaining_length, DATA_SIZE_CF);

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    send_frame_series(&ff_frame, 1, rx_addr);

    while (remaining_frames) {
        check_frame_series(&fc_frame, 1, &frame_msgq);

        if (remaining_frames >= fc_opts.bs) {
            send_frame_series(data_frame_ptr, fc_opts.bs, rx_addr);
            data_frame_ptr += fc_opts.bs;
            remaining_frames -= fc_opts.bs;
        }
        else {
            send_frame_series(data_frame_ptr, remaining_frames, rx_addr);
            data_frame_ptr += remaining_frames;
            remaining_frames = 0;
        }
    }
    /* this used to come after the next line, which drains the queue
       that had the effect of being too late to get the data from isotp_fast_recv;
       even as it is, this seems to rely on everything being slow enough to
       work correctly */
    receive_test_data(&ctx, random_data, DATA_SEND_LENGTH, 0);

    ret = k_msgq_get(&frame_msgq, &dummy_frame, K_MSEC(50));
    zassert_equal(ret, -EAGAIN, "Expected timeout but got %d", ret);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_send_timeouts)
{
    int ret;
    uint32_t start_time, time_diff;
    struct frame_desired fc_cts_frame;

    fc_cts_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_cts_frame.data[1] = FC_PCI_BYTE_2(fc_opts.bs);
    fc_cts_frame.data[2] = FC_PCI_BYTE_3(0);
    fc_cts_frame.length = DATA_SIZE_FC;

    /* Test timeout for first FC*/
    k_sem_reset(&send_compl_sem);
    start_time = k_uptime_get_32();
    isotp_fast_send(&ctx, random_data, sizeof(random_data), rx_node_id,
                    INT_TO_POINTER(ISOTP_N_TIMEOUT_BS));
    ret = k_sem_take(&send_compl_sem, K_MSEC(BS_TIMEOUT_UPPER_MS));
    time_diff = k_uptime_get_32() - start_time;
    zassert_equal(ret, 0, "Timeout too late");
    zassert_true(time_diff >= BS_TIMEOUT_LOWER_MS, "Timeout too early (%dms)", time_diff);

    /* Test timeout for consecutive FC frames */
    k_sem_reset(&send_compl_sem);
    ret = isotp_fast_send(&ctx, random_data, sizeof(random_data), rx_node_id,
                          INT_TO_POINTER(ISOTP_N_TIMEOUT_BS));
    zassert_equal(ret, ISOTP_N_OK, "Send returned %d", ret);

    send_frame_series(&fc_cts_frame, 1, rx_addr);

    start_time = k_uptime_get_32();
    ret = k_sem_take(&send_compl_sem, K_MSEC(BS_TIMEOUT_UPPER_MS));
    zassert_equal(ret, 0, "Timeout too late");

    time_diff = k_uptime_get_32() - start_time;
    zassert_true(time_diff >= BS_TIMEOUT_LOWER_MS, "Timeout too early (%dms)", time_diff);

    /* Test timeout reset with WAIT frame */
    k_sem_reset(&send_compl_sem);
    ret = isotp_fast_send(&ctx, random_data, sizeof(random_data), rx_node_id,
                          INT_TO_POINTER(ISOTP_N_TIMEOUT_BS));
    zassert_equal(ret, ISOTP_N_OK, "Send returned %d", ret);

    ret = k_sem_take(&send_compl_sem, K_MSEC(800));
    zassert_equal(ret, -EAGAIN, "Timeout too early");

    fc_cts_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    send_frame_series(&fc_cts_frame, 1, rx_addr);

    start_time = k_uptime_get_32();
    ret = k_sem_take(&send_compl_sem, K_MSEC(BS_TIMEOUT_UPPER_MS));
    zassert_equal(ret, 0, "Timeout too late");
    time_diff = k_uptime_get_32() - start_time;
    zassert_true(time_diff >= BS_TIMEOUT_LOWER_MS, "Timeout too early (%dms)", time_diff);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_receive_timeouts)
{
    int ret;
    uint32_t start_time, time_diff;
    struct frame_desired ff_frame;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], random_data, DATA_SIZE_FF);
    ff_frame.length = DATA_SIZE_FF + 2;

    send_frame_series(&ff_frame, 1, rx_addr);
    start_time = k_uptime_get_32();

    ret = blocking_recv(data_buf, sizeof(data_buf), K_FOREVER);
    zassert_equal(ret, DATA_SIZE_FF, "Expected FF data length but got %d", ret);
    ret = blocking_recv(data_buf, sizeof(data_buf), K_FOREVER);
    zassert_equal(ret, ISOTP_N_TIMEOUT_CR, "Expected timeout but got %d", ret);

    time_diff = k_uptime_get_32() - start_time;
    zassert_true(time_diff >= BS_TIMEOUT_LOWER_MS, "Timeout too early (%dms)", time_diff);
    zassert_true(time_diff <= BS_TIMEOUT_UPPER_MS, "Timeout too slow (%dms)", time_diff);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_stmin)
{
    int ret;
    struct frame_desired fc_frame, ff_frame;
    struct can_frame raw_frame;
    uint32_t start_time, time_diff;

    if (CONFIG_SYS_CLOCK_TICKS_PER_SEC < 1000) {
        /* This test requires millisecond tick resolution */
        ztest_test_skip();
    }

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SIZE_FF + DATA_SIZE_CF * 4);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SIZE_FF + DATA_SIZE_CF * 4);
    memcpy(&ff_frame.data[2], random_data, DATA_SIZE_FF);
    ff_frame.length = DATA_SIZE_FF + 2;

    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_frame.data[1] = FC_PCI_BYTE_2(2);
    fc_frame.data[2] = FC_PCI_BYTE_3(STMIN_VAL_1);
    fc_frame.length = DATA_SIZE_FC;

    filter_id = add_rx_msgq(rx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    send_test_data(random_data, DATA_SIZE_FF + DATA_SIZE_CF * 4);

    check_frame_series(&ff_frame, 1, &frame_msgq);

    send_frame_series(&fc_frame, 1, tx_addr);

    ret = k_msgq_get(&frame_msgq, &raw_frame, K_MSEC(100));
    zassert_equal(ret, 0, "Expected to get a message. [%d]", ret);

    start_time = k_uptime_get_32();
    ret = k_msgq_get(&frame_msgq, &raw_frame, K_MSEC(STMIN_VAL_1 + STMIN_UPPER_TOLERANCE));
    time_diff = k_uptime_get_32() - start_time;
    zassert_equal(ret, 0, "Expected to get a message within %dms. [%d]",
                  STMIN_VAL_1 + STMIN_UPPER_TOLERANCE, ret);
    zassert_true(time_diff >= STMIN_VAL_1, "STmin too short (%dms)", time_diff);

    fc_frame.data[2] = FC_PCI_BYTE_3(STMIN_VAL_2);
    send_frame_series(&fc_frame, 1, tx_addr);

    ret = k_msgq_get(&frame_msgq, &raw_frame, K_MSEC(100));
    zassert_equal(ret, 0, "Expected to get a message. [%d]", ret);

    start_time = k_uptime_get_32();
    ret = k_msgq_get(&frame_msgq, &raw_frame, K_MSEC(STMIN_VAL_2 + STMIN_UPPER_TOLERANCE));
    time_diff = k_uptime_get_32() - start_time;
    zassert_equal(ret, 0, "Expected to get a message within %dms. [%d]",
                  STMIN_VAL_2 + STMIN_UPPER_TOLERANCE, ret);
    zassert_true(time_diff >= STMIN_VAL_2, "STmin too short (%dms)", time_diff);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_receiver_fc_errors)
{
    int ret;
    struct frame_desired ff_frame, fc_frame;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], random_data, DATA_SIZE_FF);
    ff_frame.length = DATA_SIZE_FF + 2;

    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_CTS);
    fc_frame.data[1] = FC_PCI_BYTE_2(fc_opts.bs);
    fc_frame.data[2] = FC_PCI_BYTE_3(fc_opts.stmin);
    fc_frame.length = DATA_SIZE_FC;

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);
    zassert_true((filter_id >= 0), "Negative filter number [%d]", filter_id);

    /* wrong sequence number */
    send_frame_series(&ff_frame, 1, rx_addr);
    check_frame_series(&fc_frame, 1, &frame_msgq);

    /* original version of this test used data_buf, but then the receive just
       blocks waiting for more frames and then times out, which is the correct
       behaviour by any reasonable measure; anyway, to preserve the existing
       assertion, for now, let's pass a tiny buffer that will definitely cause
       isotp_fast_recv to return */
    uint8_t tiny_buf[CAN_MAX_DLEN];
    ret = blocking_recv(tiny_buf, sizeof(tiny_buf), K_MSEC(200));
    zassert_equal(ret, DATA_SIZE_FF, "Expected FF data length but got %d", ret);

    prepare_cf_frames(des_frames, ARRAY_SIZE(des_frames), random_data + DATA_SIZE_FF,
                      sizeof(random_data) - DATA_SIZE_FF);
    /* SN should be 2 but is set to 3 for this test */
    des_frames[1].data[0] = CF_PCI_BYTE_1 | (3 & 0x0F);
    send_frame_series(des_frames, ARRAY_SIZE(des_frames), rx_addr);

    ret = blocking_recv(data_buf, sizeof(data_buf), K_MSEC(200));
    zassert_equal(ret, ISOTP_N_WRONG_SN, "Expected wrong SN but got %d", ret);
}

ZTEST(ISOTP_FAST_CONFORMANCE_TEST_SUITE, test_sender_fc_errors)
{
    int ret, i;
    struct frame_desired ff_frame, fc_frame;

    ff_frame.data[0] = FF_PCI_BYTE_1(DATA_SEND_LENGTH);
    ff_frame.data[1] = FF_PCI_BYTE_2(DATA_SEND_LENGTH);
    memcpy(&ff_frame.data[2], random_data, DATA_SIZE_FF);
    ff_frame.length = DATA_SIZE_FF + 2;

    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);

    /* invalid flow status */
    fc_frame.data[0] = FC_PCI_BYTE_1(3);
    fc_frame.data[1] = FC_PCI_BYTE_2(fc_opts.bs);
    fc_frame.data[2] = FC_PCI_BYTE_3(fc_opts.stmin);
    fc_frame.length = DATA_SIZE_FC;

    k_sem_reset(&send_compl_sem);
    ret = isotp_fast_send(&ctx, random_data, DATA_SEND_LENGTH, rx_node_id,
                          INT_TO_POINTER(ISOTP_N_INVALID_FS));
    zassert_equal(ret, ISOTP_N_OK, "Send returned %d", ret);

    check_frame_series(&ff_frame, 1, &frame_msgq);
    send_frame_series(&fc_frame, 1, rx_addr);
    ret = k_sem_take(&send_compl_sem, K_MSEC(200));
    zassert_equal(ret, 0, "Send complete callback not called");

    /* buffer overflow */
    can_remove_rx_filter(can_dev, filter_id);

    ret = isotp_fast_send(&ctx, random_data, 5 * 1024, rx_node_id, NULL);
    zassert_equal(ret, ISOTP_N_BUFFER_OVERFLW, "Expected overflow but got %d", ret);
    filter_id = add_rx_msgq(tx_addr, CAN_EXT_ID_MASK);

    k_sem_reset(&send_compl_sem);
    ret = isotp_fast_send(&ctx, random_data, DATA_SEND_LENGTH, rx_node_id,
                          INT_TO_POINTER(ISOTP_N_BUFFER_OVERFLW));

    check_frame_series(&ff_frame, 1, &frame_msgq);
    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_OVFLW);
    send_frame_series(&fc_frame, 1, rx_addr);
    ret = k_sem_take(&send_compl_sem, K_MSEC(200));
    zassert_equal(ret, 0, "Send complete callback not called");

    /* wft overrun */
    k_sem_reset(&send_compl_sem);
    ret = isotp_fast_send(&ctx, random_data, DATA_SEND_LENGTH, rx_node_id,
                          INT_TO_POINTER(ISOTP_N_WFT_OVRN));

    check_frame_series(&ff_frame, 1, &frame_msgq);
    fc_frame.data[0] = FC_PCI_BYTE_1(FC_PCI_WAIT);
    for (i = 0; i < CONFIG_ISOTP_WFTMAX + 1; i++) {
        send_frame_series(&fc_frame, 1, rx_addr);
    }

    ret = k_sem_take(&send_compl_sem, K_MSEC(200));
    zassert_equal(ret, 0, "Send complete callback not called");
}

void *isotp_fast_conformance_setup(void)
{
    int ret;

    zassert_true(sizeof(random_data) >= sizeof(data_buf) * 2 + 10, "Test data size too small");

    zassert_true(device_is_ready(can_dev), "CAN device not ready");

    can_mode_t can_mode = CAN_MODE_LOOPBACK;
#ifdef CONFIG_CAN_FD_MODE
    can_mode |= CAN_MODE_FD;
#endif
    ret = can_set_mode(can_dev, can_mode);
    zassert_equal(ret, 0, "Failed to set loopback mode [%d]", ret);

    k_sem_init(&send_compl_sem, 0, 1);

    return NULL;
}

void isotp_fast_conformance_before(void *)
{
    int ret = can_start(can_dev);
    zassert_equal(ret, 0, "Failed to start CAN controller [%d]", ret);

    filter_id = -1;
    k_msgq_purge(&frame_msgq);

    isotp_fast_bind(&ctx, can_dev, rx_addr, &fc_opts, isotp_fast_recv_handler, NULL,
                    isotp_fast_recv_error_handler, isotp_fast_sent_handler);
}

void isotp_fast_conformance_after(void *)
{
    isotp_fast_unbind(&ctx);

    k_msgq_purge(&frame_msgq);
    if (filter_id >= 0) {
        can_remove_rx_filter(can_dev, filter_id);
    }
    can_stop(can_dev);
}

ZTEST_SUITE(ISOTP_FAST_CONFORMANCE_TEST_SUITE, NULL, isotp_fast_conformance_setup,
            isotp_fast_conformance_before, isotp_fast_conformance_after, NULL);
