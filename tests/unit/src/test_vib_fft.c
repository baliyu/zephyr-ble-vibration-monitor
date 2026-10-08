/*
 * ztest suites for src/vib_fft.c (same checks as tests/host/test_vib_fft.c),
 * run against the CMSIS-DSP that Zephyr itself builds.
 *
 * Two suites because "refuses to run before init" can only be observed once:
 * Zephyr sorts suites by name, so vib_fft_1_before_init runs before
 * vib_fft_2_analysis (which initialises the FFT in its setup).
 *
 * Noise comes from a small deterministic generator so the results do not
 * depend on the C library's rand().
 */
#include <zephyr/ztest.h>
#include <math.h>
#include <stdint.h>
#include "vib_fft.h"

#define TWO_PI 6.28318530717958647692f

static float ax[3][VIB_N];
static const float *const axes[3] = { ax[0], ax[1], ax[2] };

static const float FS = 400.0f;

static void clear_block(void)
{
	for (int a = 0; a < 3; a++) {
		for (int i = 0; i < VIB_N; i++) {
			ax[a][i] = 0.0f;
		}
	}
	for (int i = 0; i < VIB_N; i++) {
		ax[2][i] = 9.81f;                  /* gravity on z, as when lying flat */
	}
}

static void add_sine(int axis, float f, float amp, float fs, float phase)
{
	for (int i = 0; i < VIB_N; i++) {
		ax[axis][i] += amp * sinf(TWO_PI * f * (float)i / fs + phase);
	}
}

static uint32_t rng_state = 1;

static void seed(uint32_t s)
{
	rng_state = s ? s : 1;
}

static float frand(void)               /* xorshift32, uniform in [-1, 1] */
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;
	return (float)(rng_state >> 8) / (float)(1u << 24) * 2.0f - 1.0f;
}

static void add_noise(float level)
{
	for (int a = 0; a < 3; a++) {
		for (int i = 0; i < VIB_N; i++) {
			ax[a][i] += level * frand();
		}
	}
}

/* ---------------------------------------------------------------- before init */
ZTEST_SUITE(vib_fft_1_before_init, NULL, NULL, NULL, NULL, NULL);

ZTEST(vib_fft_1_before_init, test_refuses_to_run_before_init)
{
	struct vib_result r;

	clear_block();
	zassert_equal(vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r), VIB_ERR_INIT);
}

/* ---------------------------------------------------------------- analysis */
static void *init_fft(void)
{
	zassert_equal(vib_fft_init(), VIB_OK, "init (CMSIS-DSP rfft, 512 points)");
	return NULL;
}

ZTEST_SUITE(vib_fft_2_analysis, NULL, init_fft, NULL, NULL, NULL);

ZTEST(vib_fft_2_analysis, test_12p5_hz_on_z_on_top_of_gravity)
{
	struct vib_result r;

	clear_block();
	add_sine(2, 12.5f, 1.0f, FS, 0.3f);                 /* exactly on bin 16 */
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 12.5f) < 0.02f, "freq %.3f Hz", (double)r.freq_hz);
	zassert_true(fabsf(r.amp - 1.0f) < 0.02f, "amp %.3f", (double)r.amp);
	zassert_true(r.active);
}

ZTEST(vib_fft_2_analysis, test_37p3_hz_between_bins_on_x)
{
	struct vib_result r;

	clear_block();
	add_sine(0, 37.3f, 0.5f, FS, 1.0f);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 37.3f) < 0.05f, "freq %.3f Hz", (double)r.freq_hz);
	zassert_true(fabsf(r.amp - 0.5f) < 0.015f, "amp %.3f", (double)r.amp);
}

ZTEST(vib_fft_2_analysis, test_sweep_3_to_190_hz_on_all_three_axes)
{
	struct vib_result r;
	float worst_f = 0, worst_a = 0;

	for (float f = 3.0f; f < 190.0f; f += 1.37f) {
		clear_block();
		add_sine((int)(f) % 3, f, 0.8f, FS, f);
		vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
		if (fabsf(r.freq_hz - f) > worst_f) {
			worst_f = fabsf(r.freq_hz - f);
		}
		if (fabsf(r.amp - 0.8f) / 0.8f > worst_a) {
			worst_a = fabsf(r.amp - 0.8f) / 0.8f;
		}
	}
	zassert_true(worst_f < 0.06f, "worst frequency error %.3f Hz (bin = %.3f Hz)",
		     (double)worst_f, (double)(FS / VIB_N));
	zassert_true(worst_a < 0.02f, "worst amplitude error %.1f%%", (double)(100.0f * worst_a));
}

ZTEST(vib_fft_2_analysis, test_two_tones_strong_25_hz_weak_60_hz)
{
	struct vib_result r;

	clear_block();
	add_sine(1, 25.0f, 1.0f, FS, 0);
	add_sine(1, 60.0f, 0.3f, FS, 0);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 25.0f) < 0.06f, "picks %.2f Hz", (double)r.freq_hz);
}

ZTEST(vib_fft_2_analysis, test_two_tones_strong_60_hz_weak_25_hz)
{
	struct vib_result r;

	clear_block();
	add_sine(1, 25.0f, 0.3f, FS, 0);
	add_sine(1, 60.0f, 1.0f, FS, 0);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 60.0f) < 0.06f, "picks %.2f Hz", (double)r.freq_hz);
}

ZTEST(vib_fft_2_analysis, test_diagonal_vibration_amplitude_is_the_vector_length)
{
	struct vib_result r;

	clear_block();
	add_sine(0, 40.0f, 0.5f, FS, 0);
	add_sine(1, 40.0f, 0.5f, FS, 0);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.amp - 0.5f * sqrtf(2.0f)) < 0.015f, "amp %.3f, expected %.3f",
		     (double)r.amp, (double)(0.5f * sqrtf(2.0f)));
}

ZTEST(vib_fft_2_analysis, test_rms_of_a_1_0_sine_is_0p707)
{
	struct vib_result r;

	clear_block();
	add_sine(2, 50.0f, 1.0f, FS, 0);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.rms - 0.7071f) < 0.01f, "rms %.4f", (double)r.rms);
}

ZTEST(vib_fft_2_analysis, test_sensor_like_noise_only_counts_as_still)
{
	struct vib_result r;

	seed(1);
	clear_block();
	add_noise(0.01f);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_false(r.active, "should be still");
	zassert_true(r.rms < 0.05f, "rms %.4f", (double)r.rms);
}

ZTEST(vib_fft_2_analysis, test_30_hz_buried_in_noise_on_every_axis)
{
	struct vib_result r;

	seed(2);
	clear_block();
	add_sine(0, 30.0f, 0.2f, FS, 0);
	add_noise(0.1f);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 30.0f) < 0.1f, "freq %.2f Hz", (double)r.freq_hz);
	zassert_true(r.active);
}

ZTEST(vib_fft_2_analysis, test_slow_tilting_below_2_hz_is_ignored)
{
	struct vib_result r;

	clear_block();
	add_sine(2, 0.3f, 2.0f, FS, 0);
	add_sine(0, 30.0f, 0.2f, FS, 0);
	vib_fft_analyse(axes, VIB_N, FS, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 30.0f) < 0.06f, "freq %.2f Hz", (double)r.freq_hz);
}

ZTEST(vib_fft_2_analysis, test_measured_sample_rate_is_what_makes_50_hz_read_50_hz)
{
	struct vib_result r;
	const float fs_real = 399.6f;                       /* what a 2.5 ms timer really gives */

	clear_block();
	add_sine(1, 50.0f, 0.5f, fs_real, 0);
	vib_fft_analyse(axes, VIB_N, fs_real, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 50.0f) < 0.05f, "freq %.3f Hz", (double)r.freq_hz);
	vib_fft_analyse(axes, VIB_N, 400.0f, 2.0f, 0.05f, &r);
	zassert_true(fabsf(r.freq_hz - 50.0f) > 0.04f, "assuming 400 Hz would be off: %.3f Hz",
		     (double)r.freq_hz);
}

ZTEST(vib_fft_2_analysis, test_bad_arguments_are_refused)
{
	struct vib_result r;

	clear_block();
	zassert_equal(vib_fft_analyse(axes, 256, FS, 2.0f, 0.05f, &r), VIB_ERR_ARG,
		      "wrong block length");
	zassert_equal(vib_fft_analyse(axes, VIB_N, 0.0f, 2.0f, 0.05f, &r), VIB_ERR_ARG,
		      "zero sample rate");
	zassert_equal(vib_fft_analyse(axes, VIB_N, FS, 500.0f, 0.05f, &r), VIB_ERR_ARG,
		      "minimum frequency above Nyquist");
}
