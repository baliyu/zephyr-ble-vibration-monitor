/*
 * zephyr-ble-vibration-monitor - step 4: Zephyr runs on the Nano 33 BLE and
 * talks over USB.
 *
 *  - Red LED (alias led0, P0.24) toggles every 500 ms: a visible heartbeat.
 *  - Once a terminal opens the USB serial port (DTR set), prints a banner and
 *    one line per second with the uptime.
 *
 * Console over USB comes from boards/arduino_nano_33_ble_nrf52840.overlay and
 * prj.conf; the pattern is Zephyr's own samples/subsys/usb/console.
 */
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/version.h>

BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_console), zephyr_cdc_acm_uart),
	     "The console must be the USB CDC ACM UART (see the board overlay)");

#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

#define BEAT_MS 500

int main(void)
{
	const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	bool led_ok = gpio_is_ready_dt(&led) &&
		      gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) == 0;
	bool host_seen = false;
	uint32_t beat = 0;

	while (1) {
		uint32_t dtr = 0;

		if (led_ok) {
			(void)gpio_pin_toggle_dt(&led);
		}

		(void)uart_line_ctrl_get(console, UART_LINE_CTRL_DTR, &dtr);
		if (dtr) {
			if (!host_seen) {
				host_seen = true;
				printk("\n=== zephyr-ble-vibration-monitor, step 4 ===\n");
				printk("Zephyr %s on %s, LED %s\n", KERNEL_VERSION_STRING,
				       CONFIG_BOARD, led_ok ? "ok" : "NOT available");
			}
			if ((beat % 2U) == 0U) {
				printk("alive: uptime %lld ms\n", (long long)k_uptime_get());
			}
		} else {
			host_seen = false;
		}

		beat++;
		k_sleep(K_MSEC(BEAT_MS));
	}

	return 0;
}
