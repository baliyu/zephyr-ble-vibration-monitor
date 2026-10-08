# SPDX-License-Identifier: Apache-2.0
# Zephyr ztest unit tests for the board-independent modules in ../../src.
# They run on the PC as a normal program (native_sim), no board needed.
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(vibration_unit_tests)

set(APP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)

target_include_directories(app PRIVATE ${APP_SRC})
target_sources(app PRIVATE
	src/test_accel_stats.c
	src/test_vib_fft.c
	src/test_ble_fmt.c
	src/test_ble_cmd.c
	${APP_SRC}/accel_stats.c
	${APP_SRC}/vib_fft.c
	${APP_SRC}/ble_fmt.c
	${APP_SRC}/ble_cmd.c)
