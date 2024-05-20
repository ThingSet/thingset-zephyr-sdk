# Copyright (c) The ThingSet Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_THINGSET_SDK sdk.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_SDK packetizer.c)

zephyr_library_sources_ifdef(CONFIG_THINGSET_AUTH auth.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_BLE ble.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_CAN can.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_LOG_BACKEND log_backend.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_LORAWAN lorawan.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_SERIAL serial.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_SHELL shell.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_STORAGE storage_common.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_STORAGE_EEPROM storage_eeprom.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_STORAGE_FLASH storage_flash.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_WEBSOCKET websocket.c)
zephyr_library_sources_ifdef(CONFIG_THINGSET_WIFI wifi.c)

generate_inc_file_for_target(
    app
    certs/isrgrootx1.der
    certs/isrgrootx1.der.inc
    )
