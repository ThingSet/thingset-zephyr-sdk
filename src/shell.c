/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include <thingset.h>
#include <thingset/sdk.h>

#include <stdio.h>
#include <string.h>

static uint8_t req_buf[CONFIG_SHELL_CMD_BUFF_SIZE];

extern struct thingset_context ts;

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
