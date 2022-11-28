# Copyright (c) 2022 Martin Jäger

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(thingset_sample_counter)

target_include_directories(app PRIVATE src)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
