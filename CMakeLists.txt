# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
#
# This CMake file is picked by the Zephyr build system because it is defined
# as the module CMake entry point (see zephyr/module.yml).

add_subdirectory_ifdef(CONFIG_THINGSET_SDK src)
add_subdirectory_ifdef(CONFIG_ISOTP_FAST subsys/canbus/isotp_fast)

zephyr_include_directories(include)
