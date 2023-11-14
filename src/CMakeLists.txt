# Copyright (c) The ThingSet Project Contributors
# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_ISOTP_FAST app PRIVATE isotp_fast.c)

target_sources_ifdef(CONFIG_THINGSET_SDK app PRIVATE sdk.c)
target_sources_ifdef(CONFIG_THINGSET_SDK app PRIVATE packetizer.c)

target_sources_ifdef(CONFIG_THINGSET_AUTH app PRIVATE auth.c)
target_sources_ifdef(CONFIG_THINGSET_BLE app PRIVATE ble.c)
target_sources_ifdef(CONFIG_THINGSET_CAN app PRIVATE can.c)
target_sources_ifdef(CONFIG_THINGSET_LOG_BACKEND app PRIVATE log_backend.c)
target_sources_ifdef(CONFIG_THINGSET_LORAWAN app PRIVATE lorawan.c)
target_sources_ifdef(CONFIG_THINGSET_SERIAL app PRIVATE serial.c)
target_sources_ifdef(CONFIG_THINGSET_SHELL app PRIVATE shell.c)
target_sources_ifdef(CONFIG_THINGSET_STORAGE_EEPROM app PRIVATE storage_eeprom.c)
target_sources_ifdef(CONFIG_THINGSET_STORAGE_FLASH app PRIVATE storage_flash.c)
target_sources_ifdef(CONFIG_THINGSET_WEBSOCKET app PRIVATE websocket.c)
target_sources_ifdef(CONFIG_THINGSET_WIFI app PRIVATE wifi.c)

generate_inc_file_for_target(
    app
    certs/isrgrootx1.der
    certs/isrgrootx1.der.inc
    )
