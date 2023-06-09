/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_ctrl.h>

#include <thingset.h>
#include <thingset/ble.h>
#include <thingset/sdk.h>
#include <thingset/serial.h>

#include <stdio.h>

static bool panic_mode;

static uint8_t output_buf[CONFIG_THINGSET_LOG_BACKEND_BUF_SIZE];
static uint32_t log_timestamp;
static uint8_t log_msg[CONFIG_THINGSET_LOG_BACKEND_BUF_SIZE];
static uint8_t log_module[32];
uint8_t log_level;

THINGSET_ADD_GROUP(ID_ROOT, ID_LOG, "Log", THINGSET_NO_CALLBACK);
THINGSET_ADD_ITEM_UINT32(ID_LOG, 0xE0, "rUptime_s", &log_timestamp, THINGSET_ANY_R, 0);
THINGSET_ADD_ITEM_STRING(ID_LOG, 0xE1, "rMessage", log_msg, sizeof(log_msg), THINGSET_ANY_R, 0);
THINGSET_ADD_ITEM_STRING(ID_LOG, 0xE2, "oModule", log_module, sizeof(log_module), THINGSET_ANY_R,
                         0);
THINGSET_ADD_ITEM_UINT8(ID_LOG, 0xE3, "oLevel", &log_level, THINGSET_ANY_R, 0);

static int line_out(uint8_t *data, size_t len, void *ctx)
{
    /* ToDo: Handle cases where the log message does not fit into the buffer. */
    if (len > 0 && len < sizeof(log_msg)) {
        memcpy(log_msg, data, len);
        log_msg[len] = '\0';
    }

    return len;
}

/*
 * For memory optimization, the ThingSet log_msg buffer could be used directly instead of a separate
 * output_buf as long as the log messages are not longer than the buffer size.
 */
LOG_OUTPUT_DEFINE(log_output_thingset, line_out, output_buf, sizeof(output_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg_generic)
{
    struct log_msg *msg = &msg_generic->log;

    if (panic_mode) {
        return;
    }

    uint8_t domain_id = log_msg_get_domain(msg);
    int16_t source_id;

    /* function to get sname string from log_output_msg_process in subsys/logging/log_output.c */
    if (IS_ENABLED(CONFIG_LOG_MULTIDOMAIN) && domain_id != Z_LOG_LOCAL_DOMAIN_ID) {
        /* Remote domain is converting source pointer to ID */
        source_id = (int16_t)(uintptr_t)log_msg_get_source(msg);
    }
    else {
        void *source = (void *)log_msg_get_source(msg);
        if (source != NULL) {
            source_id = IS_ENABLED(CONFIG_LOG_RUNTIME_FILTERING) ? log_dynamic_source_id(source)
                                                                 : log_const_source_id(source);
        }
        else {
            source_id = -1;
        }
    }
    const char *sname = source_id >= 0 ? log_source_name_get(domain_id, source_id) : NULL;

    size_t plen;
    uint8_t *package = log_msg_get_package(msg, &plen);

    if (!sname || plen == 0) {
        return;
    }

    log_level = log_msg_get_level(msg);
    log_timestamp = log_output_timestamp_to_us(log_msg_get_timestamp(msg)) / USEC_PER_SEC;

    /* if sname does not fit into log_module, it will be truncated */
    snprintf(log_module, sizeof(log_module), "%s", sname);

    /* HEXDUMPs are ignored and the log messages should be extracted w/o line ending */
    log_output_process(&log_output_thingset, 0, NULL, NULL, log_level, package, NULL, 0,
                       LOG_OUTPUT_FLAG_CRLF_NONE);

    /* ToDo: Implement rate limit to avoid congestion */
#ifdef CONFIG_THINGSET_SERIAL
    thingset_serial_pub_report("Log");
#endif
#ifdef CONFIG_THINGSET_BLE
    thingset_ble_pub_report("Log");
#endif
}

static void panic(struct log_backend const *const backend)
{
    panic_mode = true;
}

static const struct log_backend_api log_backend_thingset_api = {
    .process = process,
    .panic = panic,
};

LOG_BACKEND_DEFINE(log_backend_thingset, log_backend_thingset_api, true);
