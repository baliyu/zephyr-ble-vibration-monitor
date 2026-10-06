# Zephyr BLE Vibration Monitor

Vibration monitor on the **Arduino Nano 33 BLE** (nRF52840, LSM9DS1 motion sensor) built on **Zephyr RTOS 4.4.2**: sensor sampling, CMSIS-DSP FFT, a secure BLE GATT service, unit tests that run on the PC, and CI.

**Status: in progress.**

## Progress
- [x] Step 1 – Identified the motion sensor from the chip itself: LSM9DS1 (accel/gyro 0x6B, WHO_AM_I `0x68`; magnetometer 0x1E), so the original Nano 33 BLE, not the Rev2
- [x] Step 2 – Zephyr 4.4.2 workspace and SDK 1.0.1 installed; blinky builds for `arduino_nano_33_ble/nrf52840`
- [x] Step 3 – Flashing from WSL through Arduino's `bossac`: found and fixed a doubled flash offset (see Lessons learned); a diagnostic blinky on the green power LED proved Zephyr runs
- [x] Step 4 – Application skeleton: console over USB (CDC ACM, appears as a COM port), red-LED heartbeat, uptime log once a second
- [x] Step 5 – LSM9DS1 accelerometer through Zephyr's sensor API, with a start-up report (I2C probe of both sensor chips, supply state, driver start). Flat: |a| = 9.57 m/s^2; tilted: 9.80 (see Lessons learned)
- [x] Step 6 – Dominant vibration frequency with a CMSIS-DSP FFT: 400 Hz sampling thread with ping-pong buffers, 512-point Hann-windowed FFT per axis, summed spectra, interpolated peak. On hardware: still / hand shaking at 2.3-3.5 Hz; FFT 2.8 ms per 1.28 s block; 0 missed ticks, read errors or dropped blocks
- [ ] BLE GATT service with notifications, LE Secure Connections pairing
- [ ] ztest unit tests on `native_sim` and GitHub Actions CI

## Layout
```
CMakeLists.txt, prj.conf       application build and Kconfig
boards/arduino_nano_33_ble_nrf52840.overlay   console over USB instead of the header UART
src/main.c                     sampler thread + analysis thread, USB console output
src/vib_fft.c                  dominant frequency, amplitude and rms (CMSIS-DSP real FFT)
src/accel_stats.c              integer statistics (step 5)
tests/host/                    PC tests: make -C tests/host (uses Zephyr's own CMSIS-DSP)
tools/flash_nano33ble.sh       guarded flashing from WSL (bossac.exe on Windows)
tools/flash_map.py             locate an image in a flash dump (diagnostic)
diagnostics/blinky_power_led.overlay   blinky on the green power LED, to prove the image runs
```
Zephyr itself lives outside the repository (`~/zephyrproject`, pinned to v4.4.2).

## How the analysis works
- **Sampling:** a high-priority thread reads the accelerometer every 2.5 ms from a kernel timer into one of two buffers. When 512 samples (1.28 s) are collected it hands the buffer to the analysis thread through a message queue and carries on in the other buffer. It records the real sample rate, missed timer ticks, read errors and the slowest read.
- **Analysis:** per axis, the mean (gravity) is removed and a Hann window applied before a CMSIS-DSP real FFT; the three power spectra are summed so the result does not depend on orientation. The peak is refined by Gaussian (log-parabolic) interpolation and the amplitude corrected for the window's scalloping loss. Below 0.05 m/s^2 rms the board counts as still; below 2 Hz is ignored (tilting).
- **Measured rate, not nominal:** a 2.5 ms timer on a 32768 Hz system clock really gives 399.6 Hz; the frequency calculation uses the measured rate (a test shows assuming 400 Hz would put 50 Hz at 50.05 Hz).
- **PC tests:** 17 tests for the FFT module, built against the exact CMSIS-DSP revision Zephyr 4.4.2 pins: worst frequency error 0.013 Hz over a 3-190 Hz sweep on all axes (bin width 0.78 Hz), worst amplitude error 0.7%, two-tone, diagonal, noise-only, buried-in-noise, slow-tilt and bad-input cases; also run under the address and undefined-behaviour sanitizers.

## Known limitations
- **Aliasing above 200 Hz.** At 400 Hz sampling, vibrations above 200 Hz appear mirrored below it (250 Hz would read about 150 Hz). The sensor runs at 952 Hz with its own filter at a few hundred Hz, which does not fully prevent this. The fix is to read the sensor's internal FIFO at its full rate, which Zephyr's LSM9DS1 driver does not support yet.
- **Sample timing.** Polling a sensor that samples on its own clock gives up to about 1 ms uncertainty in when each value was taken: fine for finding a dominant frequency, not for precise phase or amplitude work.
- **No calibration.** The Z axis reads about 2.5 % low on this board (see below).

## Build and flash (Ubuntu/WSL2)
```bash
source ~/zephyrproject/.venv/bin/activate && cd ~/zephyrproject/zephyr
west build -p always -b arduino_nano_33_ble/nrf52840 /mnt/c/zephyr-ble-vibration-monitor -d ~/build/vib
# double-tap RESET on the board, then find its COM port:
powershell.exe -NoProfile -c "[System.IO.Ports.SerialPort]::GetPortNames()"
sh /mnt/c/zephyr-ble-vibration-monitor/tools/flash_nano33ble.sh COM7
```
The flash script makes the same call as the Arduino IDE (no `--offset`, see below), refuses an image larger than the code partition, refuses to flash unless the port answers as an nRF52840 with the Arduino bootloader, and stops unless bossac exits cleanly and prints `Verify successful`.

## Lessons learned
- Read the chip, not the label: the sensor's WHO_AM_I register decided the board variant.
- In Zephyr, `led0` is whatever the board file says: on this board it is the red channel of the RGB LED, not the yellow "L" LED, which shares its pin with SPI.
- **A verified flash is not a running program.** Zephyr's bossac runner passes `-o 0x10000`, but Arduino's build of bossac (1.9.1-arduino2) already writes at the bootloader's application address, 0x10000: Arduino's own core links sketches at 0x10000 and uploads them with no offset. The offset was applied twice, the image landed at 0x20000, and the bootloader kept starting the old Arduino sketch at 0x10000 (green power LED steady, no Zephyr output). The bootloader refused flash reads, so the cause was found from Arduino's linker script and upload command, then confirmed with a blinky on the green power LED. The same problem is reported in Zephyr issues #33523 and #33352.
- Testing the flash script against a fake bossac caught two bugs before they reached hardware: piping bossac's output hid its exit status, so a failed flash would have printed "Done."
- **Defaults can switch things off.** The LSM9DS1 devicetree binding defaults the output data rate to the power-up value, which is "IMU off": without setting `imu-odr` the accelerometer reads zeros.
- **A driver that fails at boot fails silently.** Started automatically, the sensor driver reported "not ready" with its error printed before any terminal could see it. Marking the node `zephyr,deferred-init` and starting it from `main()` with `device_init()`, after a direct I2C probe of both chips' WHO_AM_I registers, made it start reliably and turned the failure into a readable report.
- **Logging over the console it depends on.** Immediate-mode logging with the console on USB made the USB stack log through the USB link it was still setting up; the board hung before enumerating. Zephyr guards only the CDC ACM class against this loop (the build warns "USBD_CDC_ACM_LOG_LEVEL forced to LOG_LEVEL_NONE"). Logging was removed; the start-up report uses plain prints.
- **Per-axis error shows up as orientation dependence.** Flat, |a| read 9.57 m/s^2; tilted so gravity fell on X and Y, 9.80. Same code, different axis: the Z axis reads about 2.5 % low on this sensor, a calibration matter, not a code fault.
- **Faster sensor, more noise.** Raising the sensor rate from 119 to 952 Hz widened its internal filter and raised the still-board noise from about 0.008 to 0.025 m/s^2 rms; the "still" threshold (0.05) still separates it.
- **Stub compiles catch real bugs.** Compiling `main.c` against minimal stand-in Zephyr headers on the PC caught a `printk` format mismatch (`int64_t` is not `long long` on every platform) before the first real build.
