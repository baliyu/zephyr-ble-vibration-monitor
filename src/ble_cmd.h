/*
 * ble_cmd.h - the two-key console command that forgets all paired phones.
 *
 * Pure C, no Zephyr: it only turns typed characters (and the passage of time)
 * into actions, so it can be tested on the PC.
 *
 *   U          arms the command ("forget ALL paired phones?")
 *   Y          within 5 s: confirm -> BLE_CMD_UNPAIR
 *   any other key, or 5 s of silence: cancel
 * Carriage return / line feed are ignored, so "U", Enter, "Y" also works.
 */
#ifndef BLE_CMD_H
#define BLE_CMD_H

#include <stdbool.h>
#include <stdint.h>

#define BLE_CMD_CONFIRM_MS 5000u

enum ble_cmd_action {
	BLE_CMD_NONE,       /* nothing to do */
	BLE_CMD_ARMED,      /* tell the person to press Y to confirm */
	BLE_CMD_UNPAIR,     /* confirmed: forget all paired phones now */
	BLE_CMD_CANCELLED,  /* a wrong key while armed */
	BLE_CMD_TIMEOUT,    /* nobody confirmed in time */
};

struct ble_cmd {
	bool armed;
	uint32_t armed_ms;
};

void ble_cmd_init(struct ble_cmd *c);

/* ch: the typed character, or -1 if none arrived. now_ms: any free-running
 * millisecond counter (wrap-around at 2^32 is handled). Call it regularly,
 * also with ch = -1, so a timeout is noticed. */
enum ble_cmd_action ble_cmd_feed(struct ble_cmd *c, int ch, uint32_t now_ms);

#endif
