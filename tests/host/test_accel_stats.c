/*
 * tests/host/test_accel_stats.c - PC tests for src/accel_stats.c
 *   make -C tests/host
 */
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "accel_stats.h"

static int failures;

#define CHECK(cond, ...) do { if (cond) { printf("[PASS] "); } else { printf("[FAIL] "); failures++; } \
	printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void)
{
	/* isqrt: exact squares, neighbours, extremes */
	CHECK(accel_isqrt64(0) == 0 && accel_isqrt64(1) == 1 && accel_isqrt64(2) == 1 &&
	      accel_isqrt64(3) == 1 && accel_isqrt64(4) == 2, "isqrt of 0..4");
	int ok = 1;
	for (uint64_t r = 0; r < 200000; r += 7) {
		if (accel_isqrt64(r * r) != r || (r > 0 && accel_isqrt64(r * r - 1) != r - 1)) { ok = 0; break; }
	}
	CHECK(ok, "isqrt(r*r) = r and isqrt(r*r - 1) = r - 1 for r up to 200000");
	CHECK(accel_isqrt64(UINT64_MAX) == 4294967295u, "isqrt(2^64 - 1) = 2^32 - 1");
	ok = 1;
	for (uint64_t v = 1; v < (1ull << 40); v = v * 3 + 1) {
		uint64_t r = accel_isqrt64(v);
		if (!(r * r <= v && (r + 1) * (r + 1) > v)) { ok = 0; break; }
	}
	CHECK(ok, "isqrt is the floor of the square root on a spread of values up to 2^40");

	/* magnitude */
	int32_t g[3] = { 0, 0, 9807 }, t[3] = { 3000, -4000, 0 }, n[3] = { -9807, 0, 0 };
	CHECK(accel_magnitude(g) == 9807, "|(0, 0, 9807)| = 9807 (1 g lying flat)");
	CHECK(accel_magnitude(t) == 5000, "|(3000, -4000, 0)| = 5000 (3-4-5 triangle, sign ignored)");
	CHECK(accel_magnitude(n) == 9807, "|(-9807, 0, 0)| = 9807");
	int32_t big[3] = { 156900, 156900, 156900 };   /* 16 g on every axis */
	CHECK(accel_magnitude(big) == (int32_t)floor(sqrt(3.0 * 156900.0 * 156900.0)),
	      "16 g on all axes: no overflow, matches double-precision floor");

	/* stats */
	struct accel_stats s;
	int32_t mean[3];
	accel_stats_reset(&s);
	CHECK(accel_stats_mean(&s, mean) == -1, "mean of an empty window is refused");
	for (int i = 0; i < 100; i++) {
		int32_t v[3] = { (i % 2) ? 20 : -20, 5, 9807 + ((i % 2) ? 100 : -100) };
		accel_stats_add(&s, v);
	}
	CHECK(accel_stats_mean(&s, mean) == 0 && mean[0] == 0 && mean[1] == 5 && mean[2] == 9807,
	      "mean of alternating samples: (%d, %d, %d), expected (0, 5, 9807)", (int)mean[0], (int)mean[1], (int)mean[2]);
	CHECK(s.n == 100, "sample count 100");
	CHECK(s.mag_min == accel_magnitude((int32_t[3]){ -20, 5, 9707 }) &&
	      s.mag_max == accel_magnitude((int32_t[3]){ 20, 5, 9907 }),
	      "min/max magnitude track the extremes (%d / %d)", (int)s.mag_min, (int)s.mag_max);

	accel_stats_reset(&s);
	for (int i = 0; i < 3; i++) { int32_t v[3] = { -7, -7, -7 }; accel_stats_add(&s, v); }
	int32_t extra[3] = { -8, -8, -8 };
	accel_stats_add(&s, extra);
	accel_stats_mean(&s, mean);
	CHECK(mean[0] == -7, "negative mean truncates toward zero (-29/4 -> %d)", (int)mean[0]);

	accel_stats_reset(&s);
	for (int i = 0; i < 100000; i++) { accel_stats_add(&s, big); }
	accel_stats_mean(&s, mean);
	CHECK(mean[0] == 156900 && s.n == 100000, "100000 samples at 16 g: sums do not overflow");

	printf("\n%s: %d failure(s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
	return failures ? 1 : 0;
}
