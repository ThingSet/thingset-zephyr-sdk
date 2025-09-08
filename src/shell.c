/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>

#include <thingset.h>
#include <thingset/sdk.h>

#include <stdio.h>
#include <string.h>

static uint8_t req_buf[CONFIG_SHELL_CMD_BUFF_SIZE];

#if defined(CONFIG_THINGSET_SHELL_REPORTING) && defined(CONFIG_THINGSET_SUBSET_LIVE_METRICS)
static struct k_work_delayable reporting_work;
#endif

static int cmd_thingset(const struct shell *shell, size_t argc, char **argv)
{
    size_t pos = 0;
    for (size_t cnt = 1; cnt < argc; cnt++) {
        int ret = snprintf(req_buf + pos, sizeof(req_buf) - pos, "%s ", argv[cnt]);
        if (ret < 0) {
            shell_print(shell, "Error: Request too large.");
            return ret;
        }
        pos += ret;
    }
    req_buf[--pos] = '\0';

    struct shared_buffer *rsp_buf = thingset_sdk_shared_buffer();
    k_sem_take(&rsp_buf->lock, K_FOREVER);

    int len = thingset_process_message(&ts, (uint8_t *)req_buf, strlen(req_buf), rsp_buf->data,
                                       rsp_buf->size);

    if (len > 0) {
        shell_print(shell, "%s", rsp_buf->data);
    }

    k_sem_give(&rsp_buf->lock);

    return 0;
}

SHELL_CMD_ARG_REGISTER(thingset, NULL, "ThingSet request", cmd_thingset, 1, 10);

#if defined(CONFIG_THINGSET_SHELL_REPORTING) && defined(CONFIG_THINGSET_SUBSET_LIVE_METRICS)

static void shell_regular_report_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    const struct shell *sh = shell_backend_uart_get_ptr();
    static int64_t pub_time;

    if (live_reporting_enable) {
        struct shared_buffer *tx_buf = thingset_sdk_shared_buffer();
        k_sem_take(&tx_buf->lock, K_FOREVER);

        int len = thingset_report_path(&ts, tx_buf->data, tx_buf->size, TS_NAME_SUBSET_LIVE,
                                       THINGSET_TXT_NAMES_VALUES);
        if (len > 0) {
            shell_print(sh, "%.*s", tx_buf->size, tx_buf->data);
        }

        k_sem_give(&tx_buf->lock);
    }

    pub_time += live_reporting_period;
    thingset_sdk_reschedule_work(dwork, K_TIMEOUT_ABS_MS(pub_time));
}

static int thingset_shell_init()
{
    k_work_init_delayable(&reporting_work, shell_regular_report_handler);
    thingset_sdk_reschedule_work(&reporting_work, K_NO_WAIT);

    return 0;
}

SYS_INIT(thingset_shell_init, APPLICATION, THINGSET_INIT_PRIORITY_DEFAULT);

#endif /* CONFIG_THINGSET_SHELL_REPORTING && CONFIG_THINGSET_SUBSET_LIVE_METRICS */
