/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

LOG_MODULE_REGISTER(thingset_storage_common, CONFIG_THINGSET_SDK_LOG_LEVEL);

static struct k_work_delayable storage_work;

static bool storage_save_allowed =
    IS_ENABLED(CONFIG_THINGSET_STORAGE_INHIBIT_OVERWRITE) ? false : true;

void thingset_storage_save_queued(bool force)
{
    if (force) {
        storage_save_allowed = true;
    }

    thingset_sdk_reschedule_work(&storage_work, K_NO_WAIT);
}

static void thingset_storage_update_handler()
{
    thingset_storage_save_queued(storage_save_allowed);
}

static void thingset_storage_save_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);

    if (storage_save_allowed) {
        thingset_storage_save();
    }
    else {
        LOG_WRN("Data not stored because previous load failed.");
    }

#ifdef CONFIG_THINGSET_STORAGE_AUTOSAVE
    thingset_sdk_reschedule_work(dwork, K_HOURS(CONFIG_THINGSET_STORAGE_AUTOSAVE_INTERVAL));
#endif
}

static int thingset_storage_init(void)
{
    int err;

    for (int i = 1; i <= CONFIG_THINGSET_STORAGE_LOAD_ATTEMPTS; i++) {
        err = thingset_storage_load();
        if (err == 0) {
            break;
        }
        LOG_WRN("Loading data from storage failed (attempt %d/%d)", i,
                CONFIG_THINGSET_STORAGE_LOAD_ATTEMPTS);
    }

    if (err == 0) {
        storage_save_allowed = true;
    }

    k_work_init_delayable(&storage_work, thingset_storage_save_handler);

    if (IS_ENABLED(CONFIG_THINGSET_STORAGE_SAVE_UPDATES)) {
        thingset_set_update_callback(&ts, TS_SUBSET_NVM, thingset_storage_update_handler);
    }

#ifdef CONFIG_THINGSET_STORAGE_AUTOSAVE
    thingset_sdk_reschedule_work(&storage_work, K_HOURS(CONFIG_THINGSET_STORAGE_AUTOSAVE_INTERVAL));
#endif

    return 0;
}

SYS_INIT(thingset_storage_init, APPLICATION, THINGSET_INIT_PRIORITY_STORAGE);
