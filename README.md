# Zephyr BLE Vibration Monitor

Vibration monitor on the **Arduino Nano 33 BLE** (nRF52840, LSM9DS1 motion sensor) built on **Zephyr RTOS 4.4.2**: sensor sampling, CMSIS-DSP FFT, a secure BLE GATT service, unit tests that run on the PC, and CI.

**Status: in progress.**

## Progress
- [x] Step 1 – Identified the motion sensor from the chip itself: LSM9DS1 (accel/gyro 0x6B, WHO_AM_I `0x68`; magnetometer 0x1E), so the original Nano 33 BLE, not the Rev2
- [x] Step 2 – Zephyr 4.4.2 workspace and SDK 1.0.1 installed; blinky builds for `arduino_nano_33_ble/nrf52840`
- [x] Step 3 – Flashed from WSL through Arduino's `bossac` at the board's code partition (0x10000, bootloader untouched); write and verify succeeded
- [ ] Step 4 – Application skeleton: console over USB (CDC ACM), red-LED heartbeat
- [ ] Sensor sampling through Zephyr's sensor API
- [ ] FFT with CMSIS-DSP: dominant frequency and RMS instead of raw samples
- [ ] BLE GATT service with notifications, LE Secure Connections pairing
- [ ] ztest unit tests on `native_sim` and GitHub Actions CI

## Layout
```
CMakeLists.txt, prj.conf       application build and Kconfig
boards/arduino_nano_33_ble_nrf52840.overlay   console over USB instead of the header UART
src/main.c                     application
tools/flash_nano33ble.sh       guarded flashing from WSL (bossac.exe on Windows)
```
Zephyr itself lives outside the repository (`~/zephyrproject`, pinned to v4.4.2).

## Build and flash (Ubuntu/WSL2)
```bash
source ~/zephyrproject/.venv/bin/activate && cd ~/zephyrproject/zephyr
west build -p always -b arduino_nano_33_ble/nrf52840 /mnt/c/zephyr-ble-vibration-monitor -d ~/build/vib
# double-tap RESET on the board, then find its COM port:
powershell.exe -NoProfile -c "[System.IO.Ports.SerialPort]::GetPortNames()"
sh /mnt/c/zephyr-ble-vibration-monitor/tools/flash_nano33ble.sh COM7
```
The flash script writes only at 0x10000, refuses an image larger than the code partition, and refuses to flash unless the port answers as an nRF52840 with the Arduino bootloader.

## Lessons learned
- Read the chip, not the label: the sensor's WHO_AM_I register decided the board variant.
- In Zephyr, `led0` is whatever the board file says: on this board it is the red channel of the RGB LED, not the yellow "L" LED, which shares its pin with SPI.
