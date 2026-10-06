/* vib_fft.c - see vib_fft.h */
#include "vib_fft.h"

#include <math.h>
#include "arm_math.h"

static arm_rfft_fast_instance_f32 rfft;
static float window[VIB_N];
static float window_sum;
static float work[VIB_N];
static float spectrum[VIB_N];
static float power[VIB_BINS];
static bool ready;

int vib_fft_init(void)
{
	if (arm_rfft_fast_init_f32(&rfft, VIB_N) != ARM_MATH_SUCCESS) {
		ready = false;
		return VIB_ERR_INIT;
	}
	window_sum = 0.0f;
	for (uint32_t i = 0; i < VIB_N; i++) {
		/* periodic Hann window: the right choice for spectral analysis */
		window[i] = 0.5f * (1.0f - cosf(2.0f * PI * (float)i / (float)VIB_N));
		window_sum += window[i];
	}
	ready = true;
	return VIB_OK;
}

/* Hann window response at fractional bin offset d, relative to d = 0 */
static float hann_gain(float d)
{
	if (fabsf(d) < 1e-6f) {
		return 1.0f;
	}
	float sinc = sinf(PI * d) / (PI * d);

	return fabsf(sinc / (1.0f - d * d));
}

int vib_fft_analyse(const float *const axis[3], uint32_t n, float fs_hz, float min_hz,
		    float rms_threshold, struct vib_result *out)
{
	float sq_sum = 0.0f;

	if (!ready) {
		return VIB_ERR_INIT;
	}
	if (n != VIB_N || !(fs_hz > 0.0f) || min_hz < 0.0f || out == NULL) {
		return VIB_ERR_ARG;
	}

	for (uint32_t k = 0; k < VIB_BINS; k++) {
		power[k] = 0.0f;
	}

	for (int a = 0; a < 3; a++) {
		const float *x = axis[a];
		float mean = 0.0f;

		for (uint32_t i = 0; i < VIB_N; i++) {
			mean += x[i];
		}
		mean /= (float)VIB_N;
		for (uint32_t i = 0; i < VIB_N; i++) {
			float d = x[i] - mean;

			sq_sum += d * d;
			work[i] = d * window[i];
		}
		arm_rfft_fast_f32(&rfft, work, spectrum, 0);   /* note: modifies work[] */
		/* packed output: [0] = DC, [1] = Nyquist, then re/im pairs for bins 1..N/2-1 */
		for (uint32_t k = 1; k < VIB_BINS; k++) {
			float re = spectrum[2 * k];
			float im = spectrum[2 * k + 1];

			power[k] += re * re + im * im;
		}
	}

	out->rms = sqrtf(sq_sum / (float)VIB_N);
	out->active = out->rms >= rms_threshold;

	uint32_t kmin = (uint32_t)ceilf(min_hz * (float)VIB_N / fs_hz);
	uint32_t kmax = VIB_BINS - 2;          /* need a neighbour on each side */

	if (kmin < 2) {
		kmin = 2;                       /* skip DC and its leakage */
	}
	if (kmin > kmax) {
		return VIB_ERR_ARG;
	}

	uint32_t k = kmin;

	for (uint32_t i = kmin + 1; i <= kmax; i++) {
		if (power[i] > power[k]) {
			k = i;
		}
	}

	/* Gaussian interpolation: parabola through the logs of the magnitudes */
	float delta = 0.0f;
	float a = power[k - 1], b = power[k], c = power[k + 1];

	if (a > 0.0f && b > 0.0f && c > 0.0f) {
		float la = 0.5f * logf(a), lb = 0.5f * logf(b), lc = 0.5f * logf(c);
		float den = la - 2.0f * lb + lc;

		if (den < 0.0f) {
			delta = 0.5f * (la - lc) / den;
		}
		if (delta > 0.5f) {
			delta = 0.5f;
		} else if (delta < -0.5f) {
			delta = -0.5f;
		}
	}

	out->peak_bin = k;
	out->freq_hz = ((float)k + delta) * fs_hz / (float)VIB_N;
	/* single-sided amplitude of a sine: 2|X|/sum(w), corrected for the off-bin loss */
	out->amp = 2.0f * sqrtf(b) / (window_sum * hann_gain(delta));
	return VIB_OK;
}
