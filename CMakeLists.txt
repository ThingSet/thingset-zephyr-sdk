# Copyright (c) 2021 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
#
# This CMake file is picked by the Zephyr build system because it is defined
# as the module CMake entry point (see zephyr/module.yml).

if (CONFIG_ISOTP_FAST OR CONFIG_THINGSET_SDK)
    add_subdirectory(src)
endif()

zephyr_include_directories(include)
