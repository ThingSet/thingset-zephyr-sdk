/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/ztest.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

/* test data objects */
static float test_float = 1234.56F;
static char test_string[] = "Hello World!";

THINGSET_ADD_GROUP(THINGSET_ID_ROOT, 0x200, "Test", THINGSET_NO_CALLBACK);
THINGSET_ADD_ITEM_FLOAT(0x200, 0x201, "wFloat", &test_float, 1, THINGSET_ANY_RW, TS_SUBSET_NVM);
THINGSET_ADD_ITEM_STRING(0x200, 0x202, "wString", test_string, sizeof(test_string), THINGSET_ANY_RW,
                         TS_SUBSET_NVM);

ZTEST(thingset_storage_eeprom, test_save_load)
{
    int err;

    err = thingset_storage_save();
    zassert_equal(err, 0);

    /* change above values */
    test_float = 0.0F;
    test_string[0] = ' ';

    err = thingset_storage_load();
    zassert_equal(err, 0);

    /* check if values were properly restored */
    zassert_equal(test_float, 1234.56F);
    zassert_mem_equal(test_string, "Hello World!", sizeof(test_string));
}

static void *thingset_storage_eeprom_setup(void)
{
    thingset_init_global(&ts);

    return NULL;
}

ZTEST_SUITE(thingset_storage_eeprom, NULL, thingset_storage_eeprom_setup, NULL, NULL, NULL);
