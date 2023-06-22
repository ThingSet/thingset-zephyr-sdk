/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include "thingset/sdk.h"

static uint32_t counter;

#define APP_ID_SAMPLE          0x05
#define APP_ID_SAMPLE_RCOUNTER 0x050
#define APP_ID_SAMPLE_WCOUNTER 0x051

THINGSET_ADD_GROUP(TS_ID_ROOT, APP_ID_SAMPLE, "Sample", THINGSET_NO_CALLBACK);

THINGSET_ADD_ITEM_UINT32(APP_ID_SAMPLE, APP_ID_SAMPLE_RCOUNTER, "rCounter", &counter,
                         THINGSET_ANY_R, TS_SUBSET_LIVE | TS_SUBSET_SUMMARY);

THINGSET_ADD_ITEM_UINT32(APP_ID_SAMPLE, APP_ID_SAMPLE_WCOUNTER, "wCounter", &counter,
                         THINGSET_ANY_RW, TS_SUBSET_LIVE | TS_SUBSET_SUMMARY);

int main(void)
{
    while (true) {
        counter++;
        k_sleep(K_SECONDS(1));
    }

    return 0;
}
