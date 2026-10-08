/*
 * zephyr-ble-vibration-monitor - step 7: dominant vibration frequency (FFT),
 * sent over BLE (ble_vib.c) as well as printed on the USB console.
 *
 * Threads:
 *  - sampler (priority 2): reads the LSM9DS1 every 2.5 ms (400 Hz) from a
 *    kernel timer into one of two buffers (ping-pong). When a block of 512
 *    samples is full it hands the buffer to main through a message queue and
 *    carries on in the other buffer. It records the real sample rate, missed
 *    timer ticks, read errors and the slowest sensor read.
 *  - main (priority 7): start-up report, then for each block: CMSIS-DSP FFT
 *    (vib_fft.c), dominant frequency, amplitude and rms, printed over USB.
 * Block = 512 samples = 1.28 s; resolution 0.78 Hz; highest frequency 200 Hz.
 */
#include <stdbool.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>
#include "vib_fft.h"
#include "ble_vib.h"

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "The console must be the USB CDC ACM UART (see the board overlay)");

#define SAMPLE_PERIOD_US 2500     /* 400 Hz */
#define HOST_WAIT_MS     30000    /* wait for a terminal at start-up */
#define MIN_FREQ_HZ      2.0f     /* ignore slow tilting and drift */
#define STILL_RMS        0.05f    /* m/s^2: below this the board counts as still */
#define SAMPLER_PRIO     2
#define SAMPLER_STACK    1024

#define IMU_NODE DT_NODELABEL(lsm9ds1)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
static const struct device *const i2c_bus = DEVICE_DT_GET(DT_BUS(IMU_NODE));
static const struct device *const vdd_env = DEVICE_DT_GET(DT_NODELABEL(vdd_env));

/* ---------------- shared between the sampler and main ---------------- */
struct block_info {
	int64_t ticks;          /* time from first to last sample, in system ticks */
	uint32_t errors;        /* failed sensor reads (previous value repeated) */
	uint32_t missed;        /* timer ticks the sampler was too late for */
	uint32_t max_read_us;   /* slowest sensor read in the block */
};

static int32_t raw[2][3][VIB_N];          /* milli-m/s^2 */
static struct block_info info[2];
static atomic_t busy[2];                   /* set while main owns the buffer */
static atomic_t overruns;                  /* blocks dropped because main was late */
K_MSGQ_DEFINE(block_q, sizeof(uint8_t), 2, 1);
K_TIMER_DEFINE(sample_timer, NULL, NULL);
K_THREAD_STACK_DEFINE(sampler_stack, SAMPLER_STACK);
static struct k_thread sampler_thread;

static int32_t to_milli(const struct sensor_value *v)
{
	return v->val1 * 1000 + v->val2 / 1000;
}

static void sampler(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
	uint8_t w = 0;                 /* buffer being filled */
	bool storing = true;           /* false while both buffers are owned by main */
	uint32_t i = 0;
	int32_t last[3] = { 0, 0, 0 };
	int64_t t_first = 0;
	struct block_info cur = { 0 };

	k_timer_start(&sample_timer, K_USEC(SAMPLE_PERIOD_US), K_USEC(SAMPLE_PERIOD_US));
	while (1) {
		struct sensor_value v[3];
		uint32_t expired = k_timer_status_sync(&sample_timer);
		uint32_t c0 = k_cycle_get_32();

		if (expired > 1) {
			cur.missed += expired - 1;
		}
		if (sensor_sample_fetch_chan(imu, SENSOR_CHAN_ACCEL_XYZ) == 0 &&
		    sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, v) == 0) {
			for (int a = 0; a < 3; a++) {
				last[a] = to_milli(&v[a]);
			}
		} else {
			cur.errors++;
		}
		uint32_t us = k_cyc_to_us_floor32(k_cycle_get_32() - c0);

		if (us > cur.max_read_us) {
			cur.max_read_us = us;
		}

		int64_t now = k_uptime_ticks();

		if (i == 0) {
			t_first = now;
		}
		if (storing) {
			for (int a = 0; a < 3; a++) {
				raw[w][a][i] = last[a];
			}
		}
		if (++i < VIB_N) {
			continue;
		}

		/* block complete */
		if (storing) {
			cur.ticks = now - t_first;
			info[w] = cur;
			atomic_set(&busy[w], 1);
			(void)k_msgq_put(&block_q, &w, K_NO_WAIT);
		} else {
			atomic_inc(&overruns);
		}
		uint8_t next = w ^ 1U;

		if (atomic_get(&busy[next]) == 0) {
			w = next;
			storing = true;
		} else {
			storing = false;   /* main still has both: skip this block */
		}
		i = 0;
		cur = (struct block_info){ 0 };
	}
}

/* ---------------- main: start-up report and analysis ---------------- */
static bool host_connected(const struct device *console)
{
	uint32_t dtr = 0;

	(void)uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
	return dtr != 0;
}

/* prints v with 1 or 3 decimals, e.g. -0.512 */
static void print_f(float v, int decimals)
{
	long long scale = (decimals == 1) ? 10 : 1000;
	long long s = (long long)(v * (float)scale + (v >= 0.0f ? 0.5f : -0.5f));
	long long whole = llabs(s) / scale, frac = llabs(s) % scale;

	if (decimals == 1) {
		printk("%s%lld.%01lld", s < 0 ? "-" : "", whole, frac);
	} else {
		printk("%s%lld.%03lld", s < 0 ? "-" : "", whole, frac);
	}
}

static void probe(const char *name, uint16_t addr, uint8_t expect)
{
	uint8_t id = 0;
	int ret = i2c_reg_read_byte(i2c_bus, addr, 0x0F, &id);

	printk("  I2C 0x%02X %-13s WHO_AM_I: ", addr, name);
	if (ret == 0) {
		printk("0x%02X %s\n", id, id == expect ? "(correct)" : "(UNEXPECTED)");
	} else {
		printk("no answer (error %d)\n", ret);
	}
}

static bool start_sensor(void)
{
	int ret;

	printk("Sensor start-up report:\n");
	printk("  I2C bus %s: %s\n", i2c_bus->name, device_is_ready(i2c_bus) ? "ready" : "NOT READY");
	printk("  sensor supply (vdd_env): %s\n",
	       !device_is_ready(vdd_env) ? "regulator NOT READY" :
	       (regulator_is_enabled(vdd_env) ? "on" : "OFF"));
	probe("accel/gyro", 0x6B, 0x68);
	probe("magnetometer", 0x1E, 0x3D);
	ret = device_init(imu);
	printk("  device_init(lsm9ds1): %d %s\n", ret, ret == 0 ? "(started)" : "(FAILED)");
	return ret == 0 && device_is_ready(imu);
}

static void print_header(void)
{
	printk("every %u ms: dominant frequency | amplitude | rms (m/s^2) | real rate | fft time | "
	       "slowest read | missed / errors / dropped\n",
	       (unsigned int)(VIB_N * SAMPLE_PERIOD_US / 1000U));
}

int main(void)
{
	const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	bool led_ok = gpio_is_ready_dt(&led) &&
		      gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) == 0;
	static float fbuf[3][VIB_N];
	const float *const axes[3] = { fbuf[0], fbuf[1], fbuf[2] };
	bool host_seen;

	for (int waited = 0; waited < HOST_WAIT_MS && !host_connected(console); waited += 100) {
		if (led_ok && (waited % 500) == 0) {
			(void)gpio_pin_toggle_dt(&led);
		}
		k_msleep(100);
	}
	host_seen = host_connected(console);

	printk("\n=== zephyr-ble-vibration-monitor, step 7 ===\n");
	printk("Zephyr %s on %s, uptime %lld ms\n", KERNEL_VERSION_STRING, CONFIG_BOARD,
	       (long long)k_uptime_get());
	if (!start_sensor()) {
		printk("LSM9DS1 NOT READY - stopping here (see the report above)\n");
		return 0;
	}
	if (vib_fft_init() != VIB_OK) {
		printk("CMSIS-DSP FFT init FAILED - stopping here\n");
		return 0;
	}
	printk("LSM9DS1 READY. Sampling 400 Hz, blocks of %d (1.28 s), resolution 0.78 Hz, "
	       "up to 200 Hz; still below rms ", VIB_N);
	print_f(STILL_RMS, 3);
	printk(" m/s^2\n");
	int ble_err = ble_vib_init();

	if (ble_err == 0) {
		printk("BLE advertising as \"%s\" (service 5f2e0001-6d1b-4a3c-9b2e-7c4d1a2b3c4d)\n",
		       CONFIG_BT_DEVICE_NAME);
	} else {
		printk("BLE start FAILED (error %d) - continuing without it\n", ble_err);
	}
	print_header();

	k_thread_create(&sampler_thread, sampler_stack, K_THREAD_STACK_SIZEOF(sampler_stack),
			sampler, NULL, NULL, NULL, SAMPLER_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&sampler_thread, "sampler");

	uint16_t seq = 0;
	enum ble_vib_state ble_prev = BLE_VIB_OFF;

	while (1) {
		uint8_t b;
		struct block_info bi;
		struct vib_result r;
		enum ble_vib_state ble_now = ble_prev;   /* unchanged unless published */

		(void)k_msgq_get(&block_q, &b, K_FOREVER);
		for (int a = 0; a < 3; a++) {
			for (int i = 0; i < VIB_N; i++) {
				fbuf[a][i] = (float)raw[b][a][i] / 1000.0f;
			}
		}
		bi = info[b];
		atomic_set(&busy[b], 0);           /* copied: the sampler may reuse it */

		float fs = (bi.ticks > 0) ?
			(float)(VIB_N - 1) * (float)CONFIG_SYS_CLOCK_TICKS_PER_SEC / (float)bi.ticks : 0.0f;
		uint32_t c0 = k_cycle_get_32();
		int ret = vib_fft_analyse(axes, VIB_N, fs, MIN_FREQ_HZ, STILL_RMS, &r);
		uint32_t fft_us = k_cyc_to_us_floor32(k_cycle_get_32() - c0);

		if (ret == VIB_OK) {
			ble_now = ble_vib_publish(&r, seq++);
		}
		if (led_ok) {
			(void)gpio_pin_toggle_dt(&led);
		}
		if (!host_connected(console)) {
			ble_prev = ble_now;
			host_seen = false;
			continue;
		}
		if (!host_seen) {
			host_seen = true;
			printk("\n(terminal connected)\n");
			print_header();
		}
		if (ble_now != ble_prev) {
			printk("BLE: %s\n", ble_vib_state_str(ble_now));
			ble_prev = ble_now;
		}
		if (ret != VIB_OK) {
			printk("analysis error %d (rate ", ret);
			print_f(fs, 1);
			printk(" Hz)\n");
			continue;
		}
		if (r.active) {
			printk("f ");
			print_f(r.freq_hz, 1);
			printk(" Hz | amp ");
			print_f(r.amp, 3);
			printk(" | rms ");
			print_f(r.rms, 3);
		} else {
			printk("still        | rms ");
			print_f(r.rms, 3);
		}
		printk(" | ");
		print_f(fs, 1);
		printk(" Hz | fft %u us | read %u us | %u/%u/%u\n", (unsigned int)fft_us,
		       (unsigned int)bi.max_read_us, (unsigned int)bi.missed, (unsigned int)bi.errors,
		       (unsigned int)atomic_get(&overruns));
	}
	return 0;
}
