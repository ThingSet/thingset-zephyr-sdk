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
 * Save data from RAM into persistent storage
 *
 * This function must not be called from a ThingSet callback context, as this would result in a
 * deadlock while. Use thingset_storage_save_queued in this case.
 */
void thingset_storage_save();

/**
 * Save data from RAM into persistent storage (via work queue)
 */
void thingset_storage_save_queued();

/**
 * Load data from persistent storage and write to variables in RAM
 */
void thingset_storage_load();

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_STORAGE_H_ */
