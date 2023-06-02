/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "thingset/sdk.h"

static uint32_t counter;

#define ID_SAMPLE 0x05

THINGSET_ADD_GROUP(ID_ROOT, ID_SAMPLE, "Sample", THINGSET_NO_CALLBACK);

THINGSET_ADD_ITEM_UINT32(ID_SAMPLE, 0x50, "rCounter", &counter, THINGSET_ANY_R,
                         SUBSET_LIVE | SUBSET_SUMMARY);

THINGSET_ADD_ITEM_UINT32(ID_SAMPLE, 0x51, "wCounter", &counter, THINGSET_ANY_RW,
                         SUBSET_LIVE | SUBSET_SUMMARY);

int main(void)
{
    while (true) {
        counter++;
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
