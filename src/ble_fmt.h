/*
 * ble_fmt.h - what the BLE characteristics carry, as pure C so it is tested
 * on the PC (tests/host). No Zephyr or Bluetooth code here.
 *
 * Measurement characteristic, 9 bytes, little-endian:
 *   byte 0     flags   bit 0 = vibrating (0 = still)
 *   bytes 1-2  uint16  dominant frequency, 0.1 Hz units   (0 when still)
 *   bytes 3-4  uint16  amplitude, milli-m/s^2             (0 when still)
 *   bytes 5-6  uint16  rms, milli-m/s^2
 *   bytes 7-8  uint16  block sequence number (wraps)
 * Values are rounded and clamped to 0..65535.
 *
 * Summary characteristic: UTF-8 text, at most 20 bytes so it fits one
 * notification at the default ATT MTU, e.g. "24.6Hz 0.51 0.37" or
 * "still rms 0.025".
 */
#ifndef BLE_FMT_H
#define BLE_FMT_H

#include <stddef.h>
#include <stdint.h>
#include "vib_fft.h"

#define BLE_MEAS_LEN 9
#define BLE_TEXT_MAX 20

void ble_meas_encode(const struct vib_result *r, uint16_t seq, uint8_t out[BLE_MEAS_LEN]);

/* Writes the text (NUL-terminated) and returns its length (<= BLE_TEXT_MAX). */
size_t ble_text_format(const struct vib_result *r, char out[BLE_TEXT_MAX + 1]);

#endif
