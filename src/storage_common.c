/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

static struct k_work_delayable storage_work;

void thingset_storage_save_queued()
{
    thingset_sdk_reschedule_work(&storage_work, K_NO_WAIT);
}

static void thingset_storage_save_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    thingset_storage_save();

    if (IS_ENABLED(CONFIG_THINGSET_STORAGE_REGULAR)) {
        thingset_sdk_reschedule_work(dwork, K_HOURS(CONFIG_THINGSET_STORAGE_INTERVAL));
    }
}

static int thingset_storage_init(void)
{
    k_work_init_delayable(&storage_work, thingset_storage_save_handler);

    if (IS_ENABLED(CONFIG_THINGSET_STORAGE_REGULAR)) {
        thingset_sdk_reschedule_work(&storage_work, K_HOURS(CONFIG_THINGSET_STORAGE_INTERVAL));
    }

    return 0;
}

SYS_INIT(thingset_storage_init, APPLICATION, THINGSET_INIT_PRIORITY_STORAGE);
