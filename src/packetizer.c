/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "packetizer.h"

int packetize(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len, int *src_pos)
{
    int pos_buf = *src_pos;
    int pos_chunk = 0;
    if (pos_buf == 0) {
        dst[pos_chunk++] = MSG_END;
    }

    while (pos_chunk < dst_len && pos_buf < src_len) {
        if (src[pos_buf] == MSG_END) {
            dst[pos_chunk++] = MSG_ESC;
            dst[pos_chunk++] = MSG_ESC_END;
        }
        else if (src[pos_buf] == MSG_SKIP) {
            dst[pos_chunk++] = MSG_ESC;
            dst[pos_chunk++] = MSG_ESC_SKIP;
        }
        else if (src[pos_buf] == MSG_ESC) {
            dst[pos_chunk++] = MSG_ESC;
            dst[pos_chunk++] = MSG_ESC_ESC;
        }
        else {
            dst[pos_chunk++] = src[pos_buf];
        }
        pos_buf++;
    }
    if (pos_chunk < dst_len - 1 && pos_buf == src_len) {
        dst[pos_chunk++] = MSG_END;
        pos_buf++;
    }

    (*src_pos) = pos_buf;
    return pos_chunk;
}

bool reassemble(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len, int *dst_pos,
                bool *escape)
{
    bool finished = true;
    for (int i = 0; i < src_len; i++) {
        uint8_t c = *(src + i);
        if (*escape) {
            if (c == MSG_ESC_END) {
                c = MSG_END;
            }
            else if (c == MSG_ESC_ESC) {
                c = MSG_ESC;
            }
            else if (c == MSG_ESC_SKIP) {
                c = MSG_SKIP;
            }
            /* else: protocol violation, pass character as is */
            (*escape) = false;
        }
        else if (c == MSG_ESC) {
            (*escape) = true;
            continue;
        }
        else if (c == MSG_SKIP) {
            continue;
        }
        else if (c == MSG_END) {
            if (finished) {
                /* previous run finished and MSG_END was used as new start byte */
                continue;
            }
            else {
                finished = true;
                return finished;
            }
        }
        else {
            finished = false;
        }
        dst[(*dst_pos)++] = c;
    }

    return finished;
}
