/* accel_stats.c - see accel_stats.h */
#include "accel_stats.h"

uint32_t accel_isqrt64(uint64_t v)
{
	/* bit-by-bit method: exact, no floating point, bounded time */
	uint64_t res = 0;
	uint64_t bit = (uint64_t)1 << 62;

	while (bit > v) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (v >= res + bit) {
			v -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)res;
}

int32_t accel_magnitude(const int32_t v[3])
{
	uint64_t sq = 0;

	for (int i = 0; i < 3; i++) {
		int64_t c = v[i];
		sq += (uint64_t)(c * c);
	}
	return (int32_t)accel_isqrt64(sq);
}

void accel_stats_reset(struct accel_stats *s)
{
	for (int i = 0; i < 3; i++) {
		s->sum[i] = 0;
	}
	s->n = 0;
	s->mag_min = INT32_MAX;
	s->mag_max = 0;
}

void accel_stats_add(struct accel_stats *s, const int32_t v[3])
{
	int32_t m = accel_magnitude(v);

	for (int i = 0; i < 3; i++) {
		s->sum[i] += v[i];
	}
	s->n++;
	if (m < s->mag_min) {
		s->mag_min = m;
	}
	if (m > s->mag_max) {
		s->mag_max = m;
	}
}

int accel_stats_mean(const struct accel_stats *s, int32_t mean[3])
{
	if (s->n == 0) {
		return -1;
	}
	for (int i = 0; i < 3; i++) {
		mean[i] = (int32_t)(s->sum[i] / (int64_t)s->n);
	}
	return 0;
}
