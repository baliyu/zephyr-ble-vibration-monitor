# PC tests for the board-independent code in src/.
# vib_fft is built against the CMSIS-DSP that Zephyr itself uses
# (west puts it in ~/zephyrproject/modules/lib/cmsis-dsp).
CMSIS_DSP ?= $(HOME)/zephyrproject/modules/lib/cmsis-dsp
CMSIS_SRC  = $(addprefix $(CMSIS_DSP)/Source/, \
	TransformFunctions/arm_rfft_fast_init_f32.c TransformFunctions/arm_rfft_fast_f32.c \
	TransformFunctions/arm_cfft_f32.c TransformFunctions/arm_cfft_radix8_f32.c \
	TransformFunctions/arm_cfft_init_f32.c TransformFunctions/arm_bitreversal2.c \
	CommonTables/arm_common_tables.c CommonTables/arm_const_structs.c)

CFLAGS = -std=c11 -Wall -Wextra -Werror -O2 -I../../src
# __GNUC_PYTHON__ lets CMSIS-DSP build on a PC without CMSIS-Core (as its Python wrapper does)
DSP_FLAGS = -D__GNUC_PYTHON__ -I$(CMSIS_DSP)/Include -I$(CMSIS_DSP)/PrivateInclude

test: test_accel_stats test_vib_fft test_ble_fmt test_ble_cmd
	./test_accel_stats
	./test_vib_fft
	./test_ble_fmt
	./test_ble_cmd

test_accel_stats: test_accel_stats.c ../../src/accel_stats.c ../../src/accel_stats.h
	$(CC) $(CFLAGS) -o $@ test_accel_stats.c ../../src/accel_stats.c -lm

test_vib_fft: test_vib_fft.c ../../src/vib_fft.c ../../src/vib_fft.h
	@test -d $(CMSIS_DSP)/Include || { echo "CMSIS-DSP not found at $(CMSIS_DSP) (set CMSIS_DSP=...)"; exit 1; }
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE $(DSP_FLAGS) -o $@ test_vib_fft.c ../../src/vib_fft.c -w $(CMSIS_SRC) -lm

test_ble_fmt: test_ble_fmt.c ../../src/ble_fmt.c ../../src/ble_fmt.h ../../src/vib_fft.h
	$(CC) $(CFLAGS) -o $@ test_ble_fmt.c ../../src/ble_fmt.c

test_ble_cmd: test_ble_cmd.c ../../src/ble_cmd.c ../../src/ble_cmd.h
	$(CC) $(CFLAGS) -o $@ test_ble_cmd.c ../../src/ble_cmd.c

clean:
	rm -f test_accel_stats test_vib_fft test_ble_fmt test_ble_cmd
.PHONY: test clean
