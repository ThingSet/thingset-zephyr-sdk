# Copyright (c) The ThingSet Project Contributors
# SPDX-License-Identifier: Apache-2.0

target_sources(app PRIVATE sdk.c)

target_sources_ifdef(CONFIG_THINGSET_BLE app PRIVATE ble.c)
target_sources_ifdef(CONFIG_THINGSET_CAN app PRIVATE can.c)
target_sources_ifdef(CONFIG_THINGSET_LOG_BACKEND app PRIVATE log_backend.c)
target_sources_ifdef(CONFIG_THINGSET_LORAWAN app PRIVATE lorawan.c)
target_sources_ifdef(CONFIG_THINGSET_SERIAL app PRIVATE serial.c)
target_sources_ifdef(CONFIG_THINGSET_SHELL app PRIVATE shell.c)
target_sources_ifdef(CONFIG_THINGSET_STORAGE app PRIVATE storage.c)

target_sources_ifdef(CONFIG_WIFI app PRIVATE wifi.c)
