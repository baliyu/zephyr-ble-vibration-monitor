/* ztest suite for src/accel_stats.c (same checks as tests/host/test_accel_stats.c) */
#include <zephyr/ztest.h>
#include <stdint.h>
#include "accel_stats.h"

ZTEST_SUITE(accel_stats, NULL, NULL, NULL, NULL, NULL);

ZTEST(accel_stats, test_isqrt_0_to_4)
{
	zassert_equal(accel_isqrt64(0), 0);
	zassert_equal(accel_isqrt64(1), 1);
	zassert_equal(accel_isqrt64(2), 1);
	zassert_equal(accel_isqrt64(3), 1);
	zassert_equal(accel_isqrt64(4), 2);
}

ZTEST(accel_stats, test_isqrt_exact_squares_and_neighbours)
{
	for (uint64_t r = 0; r < 200000; r += 7) {
		zassert_equal(accel_isqrt64(r * r), r, "isqrt(%llu^2)", (unsigned long long)r);
		if (r > 0) {
			zassert_equal(accel_isqrt64(r * r - 1), r - 1, "isqrt(%llu^2 - 1)",
				      (unsigned long long)r);
		}
	}
}

ZTEST(accel_stats, test_isqrt_max_input)
{
	zassert_equal(accel_isqrt64(UINT64_MAX), 4294967295u, "isqrt(2^64 - 1) = 2^32 - 1");
}

ZTEST(accel_stats, test_isqrt_is_floor_on_a_spread_up_to_2_pow_40)
{
	for (uint64_t v = 1; v < (1ull << 40); v = v * 3 + 1) {
		uint64_t r = accel_isqrt64(v);

		zassert_true(r * r <= v && (r + 1) * (r + 1) > v, "v = %llu", (unsigned long long)v);
	}
}

ZTEST(accel_stats, test_magnitude_known_vectors)
{
	int32_t g[3] = { 0, 0, 9807 };
	int32_t t[3] = { 3000, -4000, 0 };
	int32_t n[3] = { -9807, 0, 0 };

	zassert_equal(accel_magnitude(g), 9807, "1 g lying flat");
	zassert_equal(accel_magnitude(t), 5000, "3-4-5 triangle, sign ignored");
	zassert_equal(accel_magnitude(n), 9807, "negative axis");
}

ZTEST(accel_stats, test_magnitude_16g_all_axes_does_not_overflow)
{
	int32_t big[3] = { 156900, 156900, 156900 };

	/* floor(sqrt(3 * 156900^2)), computed in double precision on the PC */
	zassert_equal(accel_magnitude(big), 271758);
}

ZTEST(accel_stats, test_mean_of_empty_window_is_refused)
{
	struct accel_stats s;
	int32_t mean[3];

	accel_stats_reset(&s);
	zassert_equal(accel_stats_mean(&s, mean), -1);
}

ZTEST(accel_stats, test_mean_count_and_extremes_of_alternating_samples)
{
	struct accel_stats s;
	int32_t mean[3];

	accel_stats_reset(&s);
	for (int i = 0; i < 100; i++) {
		int32_t v[3] = { (i % 2) ? 20 : -20, 5, 9807 + ((i % 2) ? 100 : -100) };

		accel_stats_add(&s, v);
	}
	zassert_equal(accel_stats_mean(&s, mean), 0);
	zassert_equal(mean[0], 0);
	zassert_equal(mean[1], 5);
	zassert_equal(mean[2], 9807);
	zassert_equal(s.n, 100);
	zassert_equal(s.mag_min, accel_magnitude((int32_t[3]){ -20, 5, 9707 }));
	zassert_equal(s.mag_max, accel_magnitude((int32_t[3]){ 20, 5, 9907 }));
}

ZTEST(accel_stats, test_negative_mean_truncates_toward_zero)
{
	struct accel_stats s;
	int32_t mean[3];
	int32_t extra[3] = { -8, -8, -8 };

	accel_stats_reset(&s);
	for (int i = 0; i < 3; i++) {
		int32_t v[3] = { -7, -7, -7 };

		accel_stats_add(&s, v);
	}
	accel_stats_add(&s, extra);
	accel_stats_mean(&s, mean);
	zassert_equal(mean[0], -7, "-29/4 truncates to -7, got %d", (int)mean[0]);
}

ZTEST(accel_stats, test_100000_samples_at_16g_do_not_overflow_the_sums)
{
	struct accel_stats s;
	int32_t mean[3];
	int32_t big[3] = { 156900, 156900, 156900 };

	accel_stats_reset(&s);
	for (int i = 0; i < 100000; i++) {
		accel_stats_add(&s, big);
	}
	accel_stats_mean(&s, mean);
	zassert_equal(mean[0], 156900);
	zassert_equal(s.n, 100000);
}
