/* tests/host/test_ble_cmd.c - PC tests for src/ble_cmd.c */
#include <stdio.h>
#include "ble_cmd.h"

static int failures;
#define CHECK(cond, ...) do { if (cond) { printf("[PASS] "); } else { printf("[FAIL] "); failures++; } \
	printf(__VA_ARGS__); printf("\n"); } while (0)

int main(void)
{
	struct ble_cmd c;

	ble_cmd_init(&c);
	CHECK(ble_cmd_feed(&c, -1, 0) == BLE_CMD_NONE, "no key, nothing happens");
	CHECK(ble_cmd_feed(&c, 'Y', 10) == BLE_CMD_NONE, "Y alone does NOT unpair");
	CHECK(ble_cmd_feed(&c, 'x', 20) == BLE_CMD_NONE, "random keys are ignored when not armed");

	CHECK(ble_cmd_feed(&c, 'U', 100) == BLE_CMD_ARMED, "U arms");
	CHECK(ble_cmd_feed(&c, 'Y', 200) == BLE_CMD_UNPAIR, "U then Y unpairs");
	CHECK(ble_cmd_feed(&c, 'Y', 300) == BLE_CMD_NONE, "after unpairing the command is disarmed (Y again does nothing)");

	CHECK(ble_cmd_feed(&c, 'u', 1000) == BLE_CMD_ARMED && ble_cmd_feed(&c, 'y', 1100) == BLE_CMD_UNPAIR,
	      "lower case u / y work too");

	CHECK(ble_cmd_feed(&c, 'U', 2000) == BLE_CMD_ARMED && ble_cmd_feed(&c, 'n', 2100) == BLE_CMD_CANCELLED,
	      "any other key cancels");
	CHECK(ble_cmd_feed(&c, 'Y', 2200) == BLE_CMD_NONE, "...and Y afterwards does nothing");

	CHECK(ble_cmd_feed(&c, 'U', 3000) == BLE_CMD_ARMED && ble_cmd_feed(&c, '\r', 3100) == BLE_CMD_NONE &&
	      ble_cmd_feed(&c, '\n', 3200) == BLE_CMD_NONE && ble_cmd_feed(&c, 'Y', 3300) == BLE_CMD_UNPAIR,
	      "Enter (CR/LF) between U and Y is ignored");

	CHECK(ble_cmd_feed(&c, 'U', 4000) == BLE_CMD_ARMED && ble_cmd_feed(&c, -1, 4000 + BLE_CMD_CONFIRM_MS - 1) == BLE_CMD_NONE &&
	      ble_cmd_feed(&c, 'Y', 4000 + BLE_CMD_CONFIRM_MS - 1) == BLE_CMD_UNPAIR,
	      "Y at 4.999 s still confirms");

	CHECK(ble_cmd_feed(&c, 'U', 5000) == BLE_CMD_ARMED && ble_cmd_feed(&c, -1, 5000 + BLE_CMD_CONFIRM_MS) == BLE_CMD_TIMEOUT &&
	      ble_cmd_feed(&c, -1, 5000 + BLE_CMD_CONFIRM_MS + 100) == BLE_CMD_NONE,
	      "5 s of silence times out (reported once)");

	CHECK(ble_cmd_feed(&c, 'U', 6000) == BLE_CMD_ARMED && ble_cmd_feed(&c, 'Y', 6000 + BLE_CMD_CONFIRM_MS) == BLE_CMD_NONE &&
	      ble_cmd_feed(&c, 'Y', 6000 + BLE_CMD_CONFIRM_MS + 1) == BLE_CMD_NONE,
	      "a late Y (after the window) does NOT unpair");

	CHECK(ble_cmd_feed(&c, 'U', 7000) == BLE_CMD_ARMED && ble_cmd_feed(&c, 'U', 11000) == BLE_CMD_ARMED &&
	      ble_cmd_feed(&c, 'Y', 15500) == BLE_CMD_UNPAIR,
	      "a second U restarts the 5 s window");

	CHECK(ble_cmd_feed(&c, 'U', 0xFFFFFF00u) == BLE_CMD_ARMED && ble_cmd_feed(&c, 'Y', 0x00000064u) == BLE_CMD_UNPAIR,
	      "millisecond counter wrap-around (U just before 2^32, Y just after)");
	CHECK(ble_cmd_feed(&c, 'U', 0xFFFFFF00u) == BLE_CMD_ARMED && ble_cmd_feed(&c, -1, 0x00001500u) == BLE_CMD_TIMEOUT,
	      "timeout also works across the wrap");

	printf("\n%s: %d failure(s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures);
	return failures ? 1 : 0;
}
