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
 */
void thingset_storage_save();

/**
 * Load data from persistent storage and write to variables in RAM
 */
void thingset_storage_load();

#ifdef __cplusplus
}
#endif

#endif /* THINGSET_STORAGE_H_ */
