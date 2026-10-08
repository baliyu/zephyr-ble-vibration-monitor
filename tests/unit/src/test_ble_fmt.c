/* ztest suite for src/ble_fmt.c (same checks as tests/host/test_ble_fmt.c) */
#include <zephyr/ztest.h>
#include <math.h>
#include <string.h>
#include "ble_fmt.h"

ZTEST_SUITE(ble_fmt, NULL, NULL, NULL, NULL, NULL);

static unsigned int le16(const uint8_t *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static struct vib_result res(int active, float f, float a, float rms)
{
	struct vib_result r = { .rms = rms, .freq_hz = f, .amp = a, .peak_bin = 0, .active = active != 0 };

	return r;
}

ZTEST(ble_fmt, test_measurement_bytes_and_byte_order)
{
	uint8_t m[BLE_MEAS_LEN];
	struct vib_result r = res(1, 24.56f, 0.512f, 0.371f);

	ble_meas_encode(&r, 0x1234, m);
	zassert_equal(m[0], 1, "flags: vibrating");
	zassert_equal(le16(&m[1]), 246, "24.56 Hz in 0.1 Hz units");
	zassert_equal(le16(&m[3]), 512, "amplitude in milli-m/s^2");
	zassert_equal(le16(&m[5]), 371, "rms in milli-m/s^2");
	zassert_equal(le16(&m[7]), 0x1234, "sequence number");
	zassert_equal(m[1], 0xF6, "little-endian: low byte first");
	zassert_equal(m[2], 0x00);
	zassert_equal(m[7], 0x34);
	zassert_equal(m[8], 0x12);
}

ZTEST(ble_fmt, test_summary_text_when_vibrating)
{
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(1, 24.56f, 0.512f, 0.371f);

	ble_text_format(&r, t);
	zassert_str_equal(t, "24.6Hz 0.51 0.37");
}

ZTEST(ble_fmt, test_still_sends_zero_frequency_and_amplitude_but_keeps_rms)
{
	uint8_t m[BLE_MEAS_LEN];
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(0, 33.0f, 0.2f, 0.0254f);

	ble_meas_encode(&r, 7, m);
	zassert_equal(m[0], 0);
	zassert_equal(le16(&m[1]), 0);
	zassert_equal(le16(&m[3]), 0);
	zassert_equal(le16(&m[5]), 25);
	ble_text_format(&r, t);
	zassert_str_equal(t, "still rms 0.025");
}

ZTEST(ble_fmt, test_rounding)
{
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(1, 2.94f, 3.3906f, 2.5149f);

	ble_text_format(&r, t);
	zassert_str_equal(t, "2.9Hz 3.39 2.51");
}

ZTEST(ble_fmt, test_worst_real_case_fits_20_bytes)
{
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(1, 199.96f, 39.239f, 39.239f);
	size_t n = ble_text_format(&r, t);

	zassert_str_equal(t, "200.0Hz 39.24 39.24");
	zassert_true(n <= BLE_TEXT_MAX, "%zu bytes", n);
}

ZTEST(ble_fmt, test_absurd_values_are_clamped)
{
	uint8_t m[BLE_MEAS_LEN];
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(1, 1e9f, 1e9f, 1e9f);
	size_t n = ble_text_format(&r, t);

	ble_meas_encode(&r, 0, m);
	zassert_true(n <= BLE_TEXT_MAX, "%zu bytes", n);
	zassert_equal(le16(&m[1]), 65535);
	zassert_equal(le16(&m[3]), 65535);
}

ZTEST(ble_fmt, test_nan_and_negatives_become_zero)
{
	uint8_t m[BLE_MEAS_LEN];
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(1, NAN, -1.0f, -0.5f);

	ble_meas_encode(&r, 0, m);
	ble_text_format(&r, t);
	zassert_equal(le16(&m[1]), 0);
	zassert_equal(le16(&m[3]), 0);
	zassert_equal(le16(&m[5]), 0);
	zassert_str_equal(t, "0.0Hz 0.00 0.00");
}

ZTEST(ble_fmt, test_still_text_clamps_at_9_999)
{
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r = res(0, 0, 0, 9.9999f);
	size_t n = ble_text_format(&r, t);

	zassert_str_equal(t, "still rms 9.999");
	zassert_true(n <= BLE_TEXT_MAX);
}
