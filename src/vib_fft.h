/*
 * vib_fft.h - dominant vibration frequency from a block of 3-axis
 * accelerometer samples, using CMSIS-DSP's real FFT.
 *
 * Per axis: remove the mean (gravity and offset), apply a Hann window,
 * real FFT; the power spectra of the three axes are summed, so the result does
 * not depend on how the board is oriented. The peak bin is refined by
 * Gaussian (log-parabolic) interpolation, and the amplitude is corrected for
 * the Hann window's scalloping loss.
 *
 * Pure C + CMSIS-DSP: unit-tested on the PC (tests/host) with the same
 * CMSIS-DSP revision Zephyr uses. Not reentrant (static work buffers).
 */
#ifndef VIB_FFT_H
#define VIB_FFT_H

#include <stdbool.h>
#include <stdint.h>

#define VIB_N    512             /* samples per block (power of two) */
#define VIB_BINS (VIB_N / 2)

#define VIB_OK          0
#define VIB_ERR_INIT   -1        /* vib_fft_init() not called or failed */
#define VIB_ERR_ARG    -2        /* bad length, sample rate or frequency range */

struct vib_result {
	float rms;          /* m/s^2, vibration only (mean removed), all axes together */
	float freq_hz;      /* dominant frequency */
	float amp;          /* peak amplitude of that component, m/s^2 */
	uint32_t peak_bin;  /* FFT bin of the peak before interpolation */
	bool active;        /* rms >= threshold: false means "still" */
};

int vib_fft_init(void);

/*
 * axis[0..2]: VIB_N samples each (m/s^2), fs_hz: the real sample rate,
 * min_hz: ignore anything below (slow drift, tilting), rms_threshold: below
 * this the board counts as still.
 */
int vib_fft_analyse(const float *const axis[3], uint32_t n, float fs_hz, float min_hz,
		    float rms_threshold, struct vib_result *out);

#endif
