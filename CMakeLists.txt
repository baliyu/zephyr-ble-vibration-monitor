# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vibration_monitor)

target_sources(app PRIVATE src/main.c src/accel_stats.c src/vib_fft.c src/ble_fmt.c src/ble_vib.c src/ble_sec.c src/ble_cmd.c)
