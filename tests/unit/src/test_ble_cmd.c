/* ztest suite for src/ble_cmd.c (same checks as tests/host/test_ble_cmd.c) */
#include <zephyr/ztest.h>
#include "ble_cmd.h"

static struct ble_cmd c;

static void fresh(void *unused)
{
	ARG_UNUSED(unused);
	ble_cmd_init(&c);
}

ZTEST_SUITE(ble_cmd, NULL, NULL, fresh, NULL, NULL);

ZTEST(ble_cmd, test_no_key_nothing_happens)
{
	zassert_equal(ble_cmd_feed(&c, -1, 0), BLE_CMD_NONE);
}

ZTEST(ble_cmd, test_y_alone_does_not_unpair)
{
	zassert_equal(ble_cmd_feed(&c, 'Y', 10), BLE_CMD_NONE);
}

ZTEST(ble_cmd, test_random_keys_are_ignored_when_not_armed)
{
	zassert_equal(ble_cmd_feed(&c, 'x', 20), BLE_CMD_NONE);
}

ZTEST(ble_cmd, test_u_then_y_unpairs_and_then_disarms)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 100), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'Y', 200), BLE_CMD_UNPAIR);
	zassert_equal(ble_cmd_feed(&c, 'Y', 300), BLE_CMD_NONE, "Y again does nothing");
}

ZTEST(ble_cmd, test_lower_case_works_too)
{
	zassert_equal(ble_cmd_feed(&c, 'u', 1000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'y', 1100), BLE_CMD_UNPAIR);
}

ZTEST(ble_cmd, test_any_other_key_cancels)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 2000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'n', 2100), BLE_CMD_CANCELLED);
	zassert_equal(ble_cmd_feed(&c, 'Y', 2200), BLE_CMD_NONE, "Y afterwards does nothing");
}

ZTEST(ble_cmd, test_enter_between_u_and_y_is_ignored)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 3000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, '\r', 3100), BLE_CMD_NONE);
	zassert_equal(ble_cmd_feed(&c, '\n', 3200), BLE_CMD_NONE);
	zassert_equal(ble_cmd_feed(&c, 'Y', 3300), BLE_CMD_UNPAIR);
}

ZTEST(ble_cmd, test_y_just_inside_the_window_still_confirms)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 4000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, -1, 4000 + BLE_CMD_CONFIRM_MS - 1), BLE_CMD_NONE);
	zassert_equal(ble_cmd_feed(&c, 'Y', 4000 + BLE_CMD_CONFIRM_MS - 1), BLE_CMD_UNPAIR);
}

ZTEST(ble_cmd, test_silence_times_out_and_is_reported_once)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 5000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, -1, 5000 + BLE_CMD_CONFIRM_MS), BLE_CMD_TIMEOUT);
	zassert_equal(ble_cmd_feed(&c, -1, 5000 + BLE_CMD_CONFIRM_MS + 100), BLE_CMD_NONE);
}

ZTEST(ble_cmd, test_a_late_y_does_not_unpair)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 6000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'Y', 6000 + BLE_CMD_CONFIRM_MS), BLE_CMD_NONE);
	zassert_equal(ble_cmd_feed(&c, 'Y', 6000 + BLE_CMD_CONFIRM_MS + 1), BLE_CMD_NONE);
}

ZTEST(ble_cmd, test_a_second_u_restarts_the_window)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 7000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'U', 11000), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'Y', 15500), BLE_CMD_UNPAIR);
}

ZTEST(ble_cmd, test_millisecond_counter_wrap_around)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 0xFFFFFF00u), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, 'Y', 0x00000064u), BLE_CMD_UNPAIR, "Y just after 2^32");
}

ZTEST(ble_cmd, test_timeout_also_works_across_the_wrap)
{
	zassert_equal(ble_cmd_feed(&c, 'U', 0xFFFFFF00u), BLE_CMD_ARMED);
	zassert_equal(ble_cmd_feed(&c, -1, 0x00001500u), BLE_CMD_TIMEOUT);
}
