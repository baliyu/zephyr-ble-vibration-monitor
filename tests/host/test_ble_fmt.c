/* tests/host/test_ble_fmt.c - PC tests for src/ble_fmt.c */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ble_fmt.h"

static int failures;
#define CHECK(cond, ...) do { if (cond) { printf("[PASS] "); } else { printf("[FAIL] "); failures++; } \
	printf(__VA_ARGS__); printf("\n"); } while (0)

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | ((unsigned)p[1] << 8); }

static struct vib_result res(int active, float f, float a, float rms)
{
	struct vib_result r = { .rms = rms, .freq_hz = f, .amp = a, .peak_bin = 0, .active = active != 0 };
	return r;
}

int main(void)
{
	uint8_t m[BLE_MEAS_LEN];
	char t[BLE_TEXT_MAX + 1];
	struct vib_result r;

	r = res(1, 24.56f, 0.512f, 0.371f);
	ble_meas_encode(&r, 0x1234, m);
	CHECK(m[0] == 1 && le16(&m[1]) == 246 && le16(&m[3]) == 512 && le16(&m[5]) == 371 && le16(&m[7]) == 0x1234,
	      "24.56 Hz / 0.512 / 0.371, seq 0x1234 -> flags %u, %u (0.1 Hz), %u, %u, seq 0x%04X",
	      m[0], le16(&m[1]), le16(&m[3]), le16(&m[5]), le16(&m[7]));
	CHECK(m[1] == 0xF6 && m[2] == 0x00 && m[7] == 0x34 && m[8] == 0x12, "little-endian byte order (low byte first)");
	ble_text_format(&r, t);
	CHECK(strcmp(t, "24.6Hz 0.51 0.37") == 0, "text \"%s\"", t);

	r = res(0, 33.0f, 0.2f, 0.0254f);
	ble_meas_encode(&r, 7, m);
	CHECK(m[0] == 0 && le16(&m[1]) == 0 && le16(&m[3]) == 0 && le16(&m[5]) == 25,
	      "still: frequency and amplitude sent as 0, rms kept (%u)", le16(&m[5]));
	ble_text_format(&r, t);
	CHECK(strcmp(t, "still rms 0.025") == 0, "still text \"%s\"", t);

	r = res(1, 2.94f, 3.3906f, 2.5149f);
	ble_text_format(&r, t);
	CHECK(strcmp(t, "2.9Hz 3.39 2.51") == 0, "rounding \"%s\"", t);

	r = res(1, 199.96f, 39.239f, 39.239f);
	size_t n = ble_text_format(&r, t);
	CHECK(strcmp(t, "200.0Hz 39.24 39.24") == 0 && n <= BLE_TEXT_MAX, "worst real case (+-4 g, 200 Hz) \"%s\", %zu bytes <= 20", t, n);

	r = res(1, 1e9f, 1e9f, 1e9f);
	n = ble_text_format(&r, t);
	ble_meas_encode(&r, 0, m);
	CHECK(n <= BLE_TEXT_MAX && le16(&m[1]) == 65535 && le16(&m[3]) == 65535,
	      "absurd values clamp: text \"%s\" (%zu bytes), binary 65535", t, n);

	r = res(1, NAN, -1.0f, -0.5f);
	ble_meas_encode(&r, 0, m);
	ble_text_format(&r, t);
	CHECK(le16(&m[1]) == 0 && le16(&m[3]) == 0 && le16(&m[5]) == 0 && strcmp(t, "0.0Hz 0.00 0.00") == 0,
	      "NaN and negatives become 0 (\"%s\")", t);

	r = res(0, 0, 0, 9.9999f);
	n = ble_text_format(&r, t);
	CHECK(strcmp(t, "still rms 9.999") == 0 && n <= BLE_TEXT_MAX, "still text clamps at 9.999 (\"%s\")", t);

	printf("\n%s: %d failure(s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
	return failures ? 1 : 0;
}
