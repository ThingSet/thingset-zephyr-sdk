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

#if DT_NODE_EXISTS(DT_CHOSEN(thingset_eeprom))
#define EEPROM_DEVICE_NODE DT_CHOSEN(thingset_eeprom)
#elif DT_NODE_EXISTS(DT_ALIAS(eeprom_0))
#define EEPROM_DEVICE_NODE DT_ALIAS(eeprom_0)
#else
#define EEPROM_DEVICE_NODE DT_NODELABEL(eeprom)
#endif

/* test data objects */
static float test_float = 1234.56F;
static char test_string[] = "Hello World!";

THINGSET_ADD_GROUP(THINGSET_ID_ROOT, 0x200, "Test", THINGSET_NO_CALLBACK);
THINGSET_ADD_ITEM_FLOAT(0x200, 0x201, "sFloat", &test_float, 1, THINGSET_ANY_RW, TS_SUBSET_NVM);
THINGSET_ADD_ITEM_STRING(0x200, 0x202, "sString", test_string, sizeof(test_string), THINGSET_ANY_RW,
                         TS_SUBSET_NVM);

#ifdef CONFIG_THINGSET_STORAGE_EEPROM_PROGRESSIVE_IMPORT_EXPORT

float f32_arr[200] = { -1.1, -2.2, -3.3 };

static THINGSET_DEFINE_FLOAT_ARRAY(f32_arr_item, 1, f32_arr, ARRAY_SIZE(f32_arr));

THINGSET_ADD_ITEM_ARRAY(0x200, 0x203, "sArr", &f32_arr_item, THINGSET_ANY_RW, TS_SUBSET_NVM);

#endif

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
#ifdef CONFIG_THINGSET_STORAGE_INHIBIT_OVERWRITE
    int err;

    /* verify that EEPROM data is actually corrupted */
    err = thingset_storage_load();
    zassert_not_equal(err, 0);

    /* save without overwriting */
    thingset_storage_save_queued(false);
    k_sleep(K_MSEC(100));

    /* verify that EEPROM data is still corrupted */
    err = thingset_storage_load();
    zassert_not_equal(err, 0);

    /* force-save */
    thingset_storage_save_queued(true);
    k_sleep(K_MSEC(100));

    /* verify that EEPROM data is now valid */
    err = thingset_storage_load();
    zassert_equal(err, 0);
#endif

    return NULL;
}

#ifdef CONFIG_THINGSET_STORAGE_INHIBIT_OVERWRITE
/*
 * The EEPROM data must be corrupted before the ThingSet storage is initialized in order to
 * test the overwrite inhibit.
 */
static int invalidate_eeprom_data(void)
{
    const struct device *eeprom_dev = DEVICE_DT_GET(EEPROM_DEVICE_NODE);
    int err;

    uint8_t bad_data[8] = { 0xD, 0xE, 0xA, 0xD, 0xB, 0xE, 0xE, 0xF };

    err = eeprom_write(eeprom_dev, 0, bad_data, sizeof(bad_data));
    zassert_equal(err, 0, "Failed to write invalid data");

    return 0;
}

SYS_INIT(invalidate_eeprom_data, APPLICATION, 0);
#endif

ZTEST_SUITE(thingset_storage_eeprom, NULL, thingset_storage_eeprom_setup, NULL, NULL, NULL);
