/*
 * zephyr-ble-vibration-monitor - step 5b: read the LSM9DS1 accelerometer.
 *
 * Start-up (the sensor driver is marked zephyr,deferred-init in the overlay):
 *  1. wait up to 30 s for a terminal on the USB serial port, so the start-up
 *     report can be read (continues on its own if nobody connects);
 *  2. report the sensor supply regulator, then probe the chip directly over
 *     I2C (WHO_AM_I of the accel/gyro at 0x6B, expect 0x68, and of the
 *     magnetometer at 0x1E, expect 0x3D);
 *  3. start the driver with device_init() and print the result (the driver's
 *     own log messages appear too).
 * Then: 100 Hz sampling; every 0.5 s mean x y z, |mean|, min..max |a|,
 * measured rate and read errors (as in step 5).
 */
#include <stdbool.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>
#include "accel_stats.h"

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "The console must be the USB CDC ACM UART (see the board overlay)");

#define SAMPLE_PERIOD_MS 10      /* 100 Hz */
#define WINDOW           50      /* samples per report: 0.5 s */
#define HOST_WAIT_MS     30000   /* how long to wait for a terminal at start-up */

#define IMU_NODE DT_NODELABEL(lsm9ds1)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct device *const imu = DEVICE_DT_GET(IMU_NODE);
static const struct device *const i2c_bus = DEVICE_DT_GET(DT_BUS(IMU_NODE));
static const struct device *const vdd_env = DEVICE_DT_GET(DT_NODELABEL(vdd_env));

K_TIMER_DEFINE(sample_timer, NULL, NULL);

static bool host_connected(const struct device *console)
{
	uint32_t dtr = 0;

	(void)uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
	return dtr != 0;
}

/* sensor_value (integer + micro part, same sign) -> milli-m/s^2 */
static int32_t to_milli(const struct sensor_value *v)
{
	return v->val1 * 1000 + v->val2 / 1000;
}

static void print_milli(int32_t m)
{
	printk("%s%d.%03d", m < 0 ? "-" : " ", abs(m) / 1000, abs(m) % 1000);
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

/* Returns true if the sensor driver started. */
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

int main(void)
{
	const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	bool led_ok = gpio_is_ready_dt(&led) &&
		      gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) == 0;
	bool host_seen;
	bool imu_ok;
	struct accel_stats st;
	uint32_t errors = 0;
	int64_t window_start;

	/* 1. give the PC a chance to open the port (blink while waiting) */
	for (int waited = 0; waited < HOST_WAIT_MS && !host_connected(console); waited += 100) {
		if (led_ok && (waited % 500) == 0) {
			(void)gpio_pin_toggle_dt(&led);
		}
		k_msleep(100);
	}
	host_seen = host_connected(console);

	printk("\n=== zephyr-ble-vibration-monitor, step 5b ===\n");
	printk("Zephyr %s on %s, uptime %lld ms\n", KERNEL_VERSION_STRING, CONFIG_BOARD,
	       (long long)k_uptime_get());

	/* 2 + 3. start the sensor ourselves, with a report */
	imu_ok = start_sensor();
	printk("LSM9DS1 %s\n", imu_ok ? "READY" : "NOT READY - see the report above");
	printk("units m/s^2 | mean x y z | |mean| | min..max |a| | rate | errors\n");

	accel_stats_reset(&st);
	window_start = k_uptime_get();
	k_timer_start(&sample_timer, K_MSEC(SAMPLE_PERIOD_MS), K_MSEC(SAMPLE_PERIOD_MS));

	while (1) {
		struct sensor_value v[3];

		k_timer_status_sync(&sample_timer);     /* wait for the next 10 ms tick */

		if (imu_ok && sensor_sample_fetch_chan(imu, SENSOR_CHAN_ACCEL_XYZ) == 0 &&
		    sensor_channel_get(imu, SENSOR_CHAN_ACCEL_XYZ, v) == 0) {
			int32_t a[3] = { to_milli(&v[0]), to_milli(&v[1]), to_milli(&v[2]) };

			accel_stats_add(&st, a);
		} else {
			errors++;
		}

		if (st.n + errors < WINDOW) {
			continue;
		}

		/* ---- one report every WINDOW ticks ---- */
		int64_t now = k_uptime_get();
		int32_t elapsed = (int32_t)(now - window_start);
		int32_t mean[3];

		window_start = now;
		if (led_ok) {
			(void)gpio_pin_toggle_dt(&led);
		}

		if (host_connected(console)) {
			if (!host_seen) {
				host_seen = true;
				printk("\n(terminal connected) LSM9DS1 %s\n", imu_ok ? "READY" : "NOT READY");
				printk("units m/s^2 | mean x y z | |mean| | min..max |a| | rate | errors\n");
			}
			if (accel_stats_mean(&st, mean) == 0) {
				printk("x");
				print_milli(mean[0]);
				printk(" y");
				print_milli(mean[1]);
				printk(" z");
				print_milli(mean[2]);
				printk(" | ");
				print_milli(accel_magnitude(mean));
				printk(" | ");
				print_milli(st.mag_min);
				printk("..");
				print_milli(st.mag_max);
			} else {
				printk("no valid samples");
			}
			printk(" | %u Hz | err %u\n",
			       elapsed > 0 ? (unsigned int)((st.n + errors) * 1000U / (uint32_t)elapsed) : 0U,
			       (unsigned int)errors);
		} else {
			host_seen = false;
		}

		accel_stats_reset(&st);
		errors = 0;
	}

	return 0;
}
