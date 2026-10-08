/* ble_cmd.c - see ble_cmd.h */
#include "ble_cmd.h"

void ble_cmd_init(struct ble_cmd *c)
{
	c->armed = false;
	c->armed_ms = 0;
}

enum ble_cmd_action ble_cmd_feed(struct ble_cmd *c, int ch, uint32_t now_ms)
{
	if (c->armed && (uint32_t)(now_ms - c->armed_ms) >= BLE_CMD_CONFIRM_MS) {
		c->armed = false;           /* too late: a key now starts from scratch */
		if (ch < 0) {
			return BLE_CMD_TIMEOUT;
		}
	}
	if (ch < 0 || ch == '\r' || ch == '\n') {
		return BLE_CMD_NONE;
	}
	if (ch == 'U' || ch == 'u') {       /* (re-)arm, restarting the 5 s window */
		c->armed = true;
		c->armed_ms = now_ms;
		return BLE_CMD_ARMED;
	}
	if (c->armed) {
		c->armed = false;
		return (ch == 'Y' || ch == 'y') ? BLE_CMD_UNPAIR : BLE_CMD_CANCELLED;
	}
	return BLE_CMD_NONE;
}
