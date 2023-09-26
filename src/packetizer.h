/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MSG_END      (0x0A)
#define MSG_SKIP     (0x0D)
#define MSG_ESC      (0xCE)
#define MSG_ESC_END  (0xCA)
#define MSG_ESC_SKIP (0xCD)
#define MSG_ESC_ESC  (0xCF)

/**
 * Split the supplied source buffer into packets that fit in the destination buffer.
 * Call this method until it returns 0.
 *
 * @param src The source buffer.
 * @param src_len The size of the source buffer.
 * @param dst The destination buffer.
 * @param dst_len The size of the destination buffer.
 * @param src_pos A pointer to the current position in the source buffer.
 *
 * @returns The length of the packet in dst. When this is 0, the source buffer
 * has been completely read.
 */
int packetize(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len, int *src_pos);

/**
 * Reassemble a message that has been split into packets by the above method. When
 * the method returns true, it has finished assembling a message.
 *
 * @param src The source buffer.
 * @param src_len The size of the source buffer.
 * @param dst The destination buffer.
 * @param dst_len The size of the destination buffer.
 * @param dst_pos A pointer to the current position in the destination buffer.
 * @param escape A pointer to a boolean indicating whether the current packet has begun
 * an escape sequence.
 *
 * @returns True when a message has been completely read.
 */
bool reassemble(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len, int *dst_pos,
                bool *escape);