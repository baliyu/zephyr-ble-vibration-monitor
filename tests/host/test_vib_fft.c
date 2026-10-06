/*
 * tests/host/test_vib_fft.c - PC tests for src/vib_fft.c, built against the
 * same CMSIS-DSP revision Zephyr 4.4.2 uses (see tests/host/Makefile).
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "vib_fft.h"

static int failures;
#define CHECK(cond, ...) do { if (cond) { printf("[PASS] "); } else { printf("[FAIL] "); failures++; } \
	printf(__VA_ARGS__); printf("\n"); } while (0)

static float ax[3][VIB_N];
static const float *const axes[3] = { ax[0], ax[1], ax[2] };

static void clear(void)
{
	for (int a = 0; a < 3; a++)
		for (int i = 0; i < VIB_N; i++)
			ax[a][i] = 0.0f;
	for (int i = 0; i < VIB_N; i++)
		ax[2][i] = 9.81f;                  /* gravity on z, as when lying flat */
}

static void add_sine(int axis, float f, float amp, float fs, float phase)
{
	for (int i = 0; i < VIB_N; i++)
		ax[axis][i] += amp * sinf(2.0f * (float)M_PI * f * (float)i / fs + phase);
}

static float frand(void) { return (float)rand() / (float)RAND_MAX * 2.0f - 1.0f; }

static void add_noise(float level)
{
	for (int a = 0; a < 3; a++)
		for (int i = 0; i < VIB_N; i++)
			ax[a][i] += level * frand();
}

int main(void)
{
	struct vib_result r;
	const float fs = 400.0f;

	CHECK(vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r) == VIB_ERR_INIT, "refuses to run before init");
	CHECK(vib_fft_init() == VIB_OK, "init (CMSIS-DSP rfft, 512 points)");

	clear(); add_sine(2, 12.5f, 1.0f, fs, 0.3f);           /* exactly on bin 16 */
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 12.5f) < 0.02f && fabsf(r.amp - 1.0f) < 0.02f && r.active,
	      "12.5 Hz, 1.0 m/s^2 on z on top of gravity: %.3f Hz, amp %.3f", r.freq_hz, r.amp);

	clear(); add_sine(0, 37.3f, 0.5f, fs, 1.0f);           /* between bins */
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 37.3f) < 0.05f && fabsf(r.amp - 0.5f) < 0.015f,
	      "37.3 Hz (between bins), 0.5 m/s^2 on x: %.3f Hz, amp %.3f", r.freq_hz, r.amp);

	float worst_f = 0, worst_a = 0;
	for (float f = 3.0f; f < 190.0f; f += 1.37f) {
		clear(); add_sine((int)(f) % 3, f, 0.8f, fs, f);
		vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
		if (fabsf(r.freq_hz - f) > worst_f) worst_f = fabsf(r.freq_hz - f);
		if (fabsf(r.amp - 0.8f) / 0.8f > worst_a) worst_a = fabsf(r.amp - 0.8f) / 0.8f;
	}
	CHECK(worst_f < 0.06f && worst_a < 0.02f,
	      "sweep 3..190 Hz on all three axes: worst frequency error %.3f Hz (bin = %.3f Hz), worst amplitude error %.1f%%",
	      worst_f, fs / VIB_N, 100.0f * worst_a);

	clear(); add_sine(1, 25.0f, 1.0f, fs, 0); add_sine(1, 60.0f, 0.3f, fs, 0);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 25.0f) < 0.06f, "two tones (25 Hz strong, 60 Hz weak): picks %.2f Hz", r.freq_hz);
	clear(); add_sine(1, 25.0f, 0.3f, fs, 0); add_sine(1, 60.0f, 1.0f, fs, 0);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 60.0f) < 0.06f, "two tones (60 Hz strong): picks %.2f Hz", r.freq_hz);

	clear(); add_sine(0, 40.0f, 0.5f, fs, 0); add_sine(1, 40.0f, 0.5f, fs, 0);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.amp - 0.5f * sqrtf(2.0f)) < 0.015f,
	      "diagonal vibration (0.5 on x and y): amplitude %.3f = vector length %.3f", r.amp, 0.5f * sqrtf(2.0f));

	clear(); add_sine(2, 50.0f, 1.0f, fs, 0);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.rms - 0.7071f) < 0.01f, "rms of a 1.0 m/s^2 sine is 0.707: %.4f", r.rms);

	srand(1); clear(); add_noise(0.01f);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(!r.active && r.rms < 0.05f, "sensor-like noise only (+-0.01): still (rms %.4f)", r.rms);

	srand(2); clear(); add_sine(0, 30.0f, 0.2f, fs, 0); add_noise(0.1f);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 30.0f) < 0.1f && r.active,
	      "30 Hz, 0.2 m/s^2 buried in noise of +-0.1 on every axis: %.2f Hz", r.freq_hz);

	clear(); add_sine(2, 0.3f, 2.0f, fs, 0); add_sine(0, 30.0f, 0.2f, fs, 0);
	vib_fft_analyse(axes, VIB_N, fs, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 30.0f) < 0.06f, "slow 0.3 Hz tilting (2.0) ignored below 2 Hz, 30 Hz found: %.2f Hz", r.freq_hz);

	const float fs_real = 399.6f;                           /* what a 2.5 ms timer really gives */
	clear(); add_sine(1, 50.0f, 0.5f, fs_real, 0);
	vib_fft_analyse(axes, VIB_N, fs_real, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 50.0f) < 0.05f, "measured rate 399.6 Hz used: 50 Hz reported as %.3f Hz", r.freq_hz);
	vib_fft_analyse(axes, VIB_N, 400.0f, 2.0f, 0.05f, &r);
	CHECK(fabsf(r.freq_hz - 50.0f) > 0.04f, "...and assuming 400 Hz would be off: %.3f Hz", r.freq_hz);

	CHECK(vib_fft_analyse(axes, 256, fs, 2.0f, 0.05f, &r) == VIB_ERR_ARG, "wrong block length refused");
	CHECK(vib_fft_analyse(axes, VIB_N, 0.0f, 2.0f, 0.05f, &r) == VIB_ERR_ARG, "zero sample rate refused");
	CHECK(vib_fft_analyse(axes, VIB_N, fs, 500.0f, 0.05f, &r) == VIB_ERR_ARG, "minimum frequency above Nyquist refused");

	printf("\n%s: %d failure(s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
	return failures ? 1 : 0;
}
