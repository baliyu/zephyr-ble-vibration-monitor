/*
 * accel_stats.h - integer statistics over a window of accelerometer samples.
 *
 * Units are milli-m/s^2 (1 g = 9807). Pure C, no Zephyr dependency, so it is
 * unit-tested on the PC (tests/host) before it runs on the board.
 */
#ifndef ACCEL_STATS_H
#define ACCEL_STATS_H

#include <stdint.h>

struct accel_stats {
	int64_t sum[3];       /* per-axis sum of samples */
	uint32_t n;           /* number of samples */
	int32_t mag_min;      /* smallest per-sample magnitude */
	int32_t mag_max;      /* largest per-sample magnitude */
};

/* Integer square root: largest r with r*r <= v. */
uint32_t accel_isqrt64(uint64_t v);

/* |v| in the same units, rounded down. */
int32_t accel_magnitude(const int32_t v[3]);

void accel_stats_reset(struct accel_stats *s);
void accel_stats_add(struct accel_stats *s, const int32_t v[3]);

/* Per-axis mean (integer division, truncates toward zero). Returns -1 if empty. */
int accel_stats_mean(const struct accel_stats *s, int32_t mean[3]);

#endif
