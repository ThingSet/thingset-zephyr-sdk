# Copyright (c) The ThingSet Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(thingset_sample_counter)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE src/main.c)
