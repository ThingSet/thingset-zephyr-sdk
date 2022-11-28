# Copyright (c) The ThingSet Project Contributors
# SPDX-License-Identifier: Apache-2.0

target_sources(app PRIVATE thingset_sdk.c)

target_sources_ifdef(CONFIG_THINGSET_BLE app PRIVATE thingset_ble.c)
target_sources_ifdef(CONFIG_THINGSET_CAN app PRIVATE thingset_can.c)
target_sources_ifdef(CONFIG_THINGSET_LORAWAN app PRIVATE thingset_lorawan.c)
target_sources_ifdef(CONFIG_THINGSET_SERIAL app PRIVATE thingset_serial.c)
target_sources_ifdef(CONFIG_THINGSET_STORAGE app PRIVATE thingset_storage.c)

# determine git tag and commit hash for automatic firmware versioning
find_package(Git)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --long --dirty --tags --always
        OUTPUT_VARIABLE FIRMWARE_VERSION_ID
        OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
    set(FIRMWARE_VERSION_ID "unknown")
endif()
zephyr_compile_definitions(FIRMWARE_VERSION_ID="${FIRMWARE_VERSION_ID}")
