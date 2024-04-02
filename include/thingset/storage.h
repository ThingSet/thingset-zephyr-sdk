/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef THINGSET_STORAGE_H_
#define THINGSET_STORAGE_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 *
 * @brief Handling of Flash or EEPROM to store device configuration
 */

/**
 * Load data from persistent storage and write to variables in RAM
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_storage_load();

/**
 * Save data from RAM into persistent storage
 *
 * This function must not be called from a ThingSet callback context, as this would result in a
 * deadlock while. Use thingset_storage_save_queued in this case.
 *
 * @returns 0 for success or negative errno in case of error
 */
int thingset_storage_save();

/**
 * Save data from RAM into persistent storage (via work queue)
 *
 * @param force Overwrite data even if loading data at boot failed.
 */
void thingset_storage_save_queued(bool force);

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_STORAGE_H_ */
