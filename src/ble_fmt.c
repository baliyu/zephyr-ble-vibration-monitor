/* ble_fmt.c - see ble_fmt.h */
#include "ble_fmt.h"

#include <stdio.h>

/* round(v * scale), clamped to 0..max; NaN and negatives give 0 */
static uint32_t scaled(float v, float scale, uint32_t max)
{
	float s = v * scale + 0.5f;

	if (!(s > 0.0f)) {
		return 0;
	}
	if (s >= (float)max) {
		return max;
	}
	return (uint32_t)s;
}

static void put_le16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFU);
	p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

void ble_meas_encode(const struct vib_result *r, uint16_t seq, uint8_t out[BLE_MEAS_LEN])
{
	out[0] = r->active ? 0x01U : 0x00U;
	put_le16(&out[1], r->active ? scaled(r->freq_hz, 10.0f, 0xFFFFU) : 0U);
	put_le16(&out[3], r->active ? scaled(r->amp, 1000.0f, 0xFFFFU) : 0U);
	put_le16(&out[5], scaled(r->rms, 1000.0f, 0xFFFFU));
	put_le16(&out[7], seq);
}

size_t ble_text_format(const struct vib_result *r, char out[BLE_TEXT_MAX + 1])
{
	int n;

	if (r->active) {
		uint32_t f = scaled(r->freq_hz, 10.0f, 9999U);    /* <= 999.9 Hz */
		uint32_t a = scaled(r->amp, 100.0f, 9999U);       /* <= 99.99 */
		uint32_t m = scaled(r->rms, 100.0f, 9999U);

		n = snprintf(out, BLE_TEXT_MAX + 1, "%u.%uHz %u.%02u %u.%02u",
			     (unsigned)(f / 10U), (unsigned)(f % 10U),
			     (unsigned)(a / 100U), (unsigned)(a % 100U),
			     (unsigned)(m / 100U), (unsigned)(m % 100U));
	} else {
		uint32_t m = scaled(r->rms, 1000.0f, 9999U);       /* <= 9.999 */

		n = snprintf(out, BLE_TEXT_MAX + 1, "still rms %u.%03u",
			     (unsigned)(m / 1000U), (unsigned)(m % 1000U));
	}
	if (n < 0) {
		out[0] = '\0';
		return 0;
	}
	return (size_t)n > BLE_TEXT_MAX ? BLE_TEXT_MAX : (size_t)n;
}
