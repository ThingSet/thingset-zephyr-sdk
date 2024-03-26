/*
 * Copyright (c) The ThingSet Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/drivers/eeprom.h>
#include <zephyr/ztest.h>

#include <thingset.h>
#include <thingset/sdk.h>
#include <thingset/storage.h>

#define EEPROM_DEVICE_NODE DT_CHOSEN(thingset_eeprom)

/* test data objects */
static float test_float = 1234.56F;
static char test_string[] = "Hello World!";

THINGSET_ADD_GROUP(THINGSET_ID_ROOT, 0x200, "Test", THINGSET_NO_CALLBACK);
THINGSET_ADD_ITEM_FLOAT(0x200, 0x201, "wFloat", &test_float, 1, THINGSET_ANY_RW, TS_SUBSET_NVM);
THINGSET_ADD_ITEM_STRING(0x200, 0x202, "wString", test_string, sizeof(test_string), THINGSET_ANY_RW,
                         TS_SUBSET_NVM);

static void corrupt_data()
{
    const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_DEVICE_NODE);
    int err;

    uint8_t zeros[4] = { 0 };

    /* write some zeros behind the header to make the CRC calculation fail */
    err = eeprom_write(eeprom_dev, 8, zeros, sizeof(zeros));
    zassert_equal(err, 0, "Failed to corrupt the data");
}

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

ZTEST(thingset_storage_eeprom, test_save_load_corrupted)
{
    int err;

    err = thingset_storage_save();
    zassert_equal(err, 0);

    /* change above values */
    test_float = 0.0F;
    test_string[0] = ' ';

    corrupt_data();

    err = thingset_storage_load();
#ifdef CONFIG_THINGSET_STORAGE_EEPROM_DUPLICATE
    zassert_equal(err, 0);

    /* check if values were properly restored */
    zassert_equal(test_float, 1234.56F);
    zassert_mem_equal(test_string, "Hello World!", sizeof(test_string));
#else
    zassert_not_equal(err, 0);
#endif
}

static void *thingset_storage_eeprom_setup(void)
{
    thingset_init_global(&ts);

    return NULL;
}

ZTEST_SUITE(thingset_storage_eeprom, NULL, thingset_storage_eeprom_setup, NULL, NULL, NULL);
