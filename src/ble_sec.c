/* ble_sec.c - see ble_sec.h. API use checked against Zephyr v4.4.2:
 * include/zephyr/bluetooth/conn.h (bt_conn_auth_cb, bt_conn_auth_info_cb,
 * bt_security_err) and subsys/bluetooth/host/smp.c. */
#include "ble_sec.h"
#include "ble_cmd.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>

/* The security policy lives in prj.conf. If someone weakens it there, the
 * build must fail instead of silently shipping an open device. */
BUILD_ASSERT(IS_ENABLED(CONFIG_BT_SMP_SC_ONLY),
	     "step 8: prj.conf must set CONFIG_BT_SMP_SC_ONLY=y (authenticated LE Secure Connections)");
BUILD_ASSERT(IS_ENABLED(CONFIG_BT_SETTINGS),
	     "step 8b: prj.conf must set CONFIG_BT_SETTINGS=y (pairing keys kept in flash)");
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_FIXED_PASSKEY),
	     "step 8: CONFIG_BT_FIXED_PASSKEY must stay off (a fixed passkey is public)");

static const char *sec_err_str(enum bt_security_err err)
{
	switch (err) {
	case BT_SECURITY_ERR_SUCCESS:           return "ok";
	case BT_SECURITY_ERR_AUTH_FAIL:         return "authentication failed (wrong passkey?)";
	case BT_SECURITY_ERR_PIN_OR_KEY_MISSING: return "key missing (does this side still have the bond?)";
	case BT_SECURITY_ERR_OOB_NOT_AVAILABLE: return "OOB data not available";
	case BT_SECURITY_ERR_AUTH_REQUIREMENT:  return "required security level not reachable";
	case BT_SECURITY_ERR_PAIR_NOT_SUPPORTED: return "pairing not supported";
	case BT_SECURITY_ERR_PAIR_NOT_ALLOWED:  return "pairing not allowed";
	case BT_SECURITY_ERR_INVALID_PARAM:     return "invalid parameters";
	case BT_SECURITY_ERR_KEY_REJECTED:      return "distributed key rejected";
	case BT_SECURITY_ERR_UNSPECIFIED:       return "unspecified";
	default:                                return "unknown";
	}
}

static void peer_str(struct bt_conn *conn, char *buf, size_t len)
{
	bt_addr_le_to_str(bt_conn_get_dst(conn), buf, len);
}

/* Called by the stack when the phone must type a passkey. The value is
 * 0..999999 and has to be shown zero-padded to six digits (Zephyr's docs). */
static void passkey_display(struct bt_conn *conn, unsigned int passkey)
{
	char addr[BT_ADDR_LE_STR_LEN];

	peer_str(conn, addr, sizeof(addr));
	printk("\n*** BLE pairing request from %s ***\n"
	       "*** Passkey: %06u  - type it on the phone ***\n\n", addr, passkey);
}

/* Required whenever passkey_display is set: the stack tells us to stop showing it. */
static void auth_cancel(struct bt_conn *conn)
{
	char addr[BT_ADDR_LE_STR_LEN];

	peer_str(conn, addr, sizeof(addr));
	printk("BLE pairing cancelled (%s)\n", addr);
}

static const struct bt_conn_auth_cb auth_cb = {
	.passkey_display = passkey_display,
	.cancel = auth_cancel,
};

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	char addr[BT_ADDR_LE_STR_LEN];

	peer_str(conn, addr, sizeof(addr));
	printk("BLE pairing complete with %s (bonded: %s)\n", addr, bonded ? "yes" : "no");
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	peer_str(conn, addr, sizeof(addr));
	printk("BLE pairing FAILED with %s: %s (code %d)\n", addr, sec_err_str(reason), (int)reason);
}

static void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	char addr[BT_ADDR_LE_STR_LEN];

	ARG_UNUSED(id);
	bt_addr_le_to_str(peer, addr, sizeof(addr));
	printk("BLE bond deleted for %s\n", addr);
}

static struct bt_conn_auth_info_cb auth_info_cb = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
	.bond_deleted = bond_deleted,
};

/* Level 4 = authenticated LE Secure Connections with a 128-bit key. Seeing it
 * again on a reconnection means the stored bond worked without a new passkey. */
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	peer_str(conn, addr, sizeof(addr));
	if (err == BT_SECURITY_ERR_SUCCESS) {
		printk("BLE link to %s is now at security level %d\n", addr, (int)level);
	} else {
		printk("BLE security change for %s failed: %s (code %d)\n",
		       addr, sec_err_str(err), (int)err);
	}
}

BT_CONN_CB_DEFINE(sec_conn_callbacks) = {
	.security_changed = security_changed,
};

int ble_sec_init(void)
{
	int err = bt_conn_auth_cb_register(&auth_cb);

	if (err) {
		return err;
	}
	return bt_conn_auth_info_cb_register(&auth_info_cb);
}

/* ---- step 8b: forgetting all paired phones from the USB console ---------- */

#define CMD_STACK_SIZE 2048    /* precaution: bt_unpair() ends in flash (NVS) writes */
#define CMD_PRIO       10      /* below main (7) and the sampler (2) */

static K_THREAD_STACK_DEFINE(cmd_stack, CMD_STACK_SIZE);
static struct k_thread cmd_thread_data;

static void cmd_thread(void *p1, void *p2, void *p3)
{
	const struct device *const console = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
	struct ble_cmd cmd;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	ble_cmd_init(&cmd);
	if (!device_is_ready(console)) {
		return;
	}
	while (true) {
		unsigned char ch = 0;
		int c = (uart_poll_in(console, &ch) == 0) ? (int)ch : -1;

		switch (ble_cmd_feed(&cmd, c, k_uptime_get_32())) {
		case BLE_CMD_ARMED:
			printk("\nForget ALL paired phones? Press Y within %u s to confirm, any other key cancels.\n",
			       BLE_CMD_CONFIRM_MS / 1000u);
			break;
		case BLE_CMD_UNPAIR: {
			int err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);

			if (err) {
				printk("Forgetting the paired phones FAILED (error %d)\n", err);
			} else {
				printk("All paired phones forgotten. On each iPhone also use "
				       "Settings > Bluetooth > (i) > Forget This Device.\n");
			}
			break;
		}
		case BLE_CMD_CANCELLED:
			printk("Cancelled, nobody was forgotten.\n");
			break;
		case BLE_CMD_TIMEOUT:
			printk("No confirmation, nobody was forgotten.\n");
			break;
		default:
			break;
		}
		if (c < 0) {
			k_msleep(50);     /* nothing typed: poll again shortly */
		}
	}
}

static void count_bond(const struct bt_bond_info *info, void *user_data)
{
	ARG_UNUSED(info);
	(*(int *)user_data)++;
}

int ble_sec_start(void)
{
	int bonds = 0;
	k_tid_t tid;

	bt_foreach_bond(BT_ID_DEFAULT, count_bond, &bonds);
	printk("BLE: %d paired phone(s) restored from flash. Console: U then Y forgets them all.\n", bonds);

	tid = k_thread_create(&cmd_thread_data, cmd_stack, K_THREAD_STACK_SIZEOF(cmd_stack),
			      cmd_thread, NULL, NULL, NULL, CMD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(tid, "ble_cmd");
	return 0;
}
