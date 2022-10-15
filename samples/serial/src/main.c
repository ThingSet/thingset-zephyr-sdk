/*
 * Copyright (c) 2022 Martin Jäger
 */

#include <zephyr/kernel.h>

#include "thingset/sdk.h"
#include "thingset/serial.h"

static uint32_t counter;

#define ID_SAMPLE 0x05

TS_ADD_GROUP(ID_SAMPLE, "Sample", TS_NO_CALLBACK, ID_ROOT);

TS_ADD_ITEM_UINT32(0x50, "rCounter", &counter, ID_SAMPLE, TS_ANY_R, SUBSET_LIVE);

void main(void)
{
    while (true) {
        counter++;
        k_sleep(K_SECONDS(1));
    }
}
