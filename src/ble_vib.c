/* ble_vib.c - see ble_vib.h. API use follows Zephyr 4.4.2's
 * samples/bluetooth/peripheral and peripheral_hr. */
#include "ble_vib.h"
#include "ble_fmt.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

/* Custom 128-bit UUIDs (randomly chosen base, ...0001 = service) */
#define VIB_SVC_UUID_VAL  BT_UUID_128_ENCODE(0x5f2e0001, 0x6d1b, 0x4a3c, 0x9b2e, 0x7c4d1a2b3c4d)
#define VIB_MEAS_UUID_VAL BT_UUID_128_ENCODE(0x5f2e0002, 0x6d1b, 0x4a3c, 0x9b2e, 0x7c4d1a2b3c4d)
#define VIB_TEXT_UUID_VAL BT_UUID_128_ENCODE(0x5f2e0003, 0x6d1b, 0x4a3c, 0x9b2e, 0x7c4d1a2b3c4d)

static const struct bt_uuid_128 svc_uuid = BT_UUID_INIT_128(VIB_SVC_UUID_VAL);
static const struct bt_uuid_128 meas_uuid = BT_UUID_INIT_128(VIB_MEAS_UUID_VAL);
static const struct bt_uuid_128 text_uuid = BT_UUID_INIT_128(VIB_TEXT_UUID_VAL);

/* latest values, read by the Bluetooth thread, written by main */
static struct k_spinlock lock;
static uint8_t meas_val[BLE_MEAS_LEN];
static char text_val[BLE_TEXT_MAX + 1] = "waiting";
static size_t text_len = 7;

static atomic_t meas_sub;      /* notifications enabled on the measurement */
static atomic_t text_sub;      /* ... on the text summary */
static atomic_t conn_count;
static atomic_t adv_needed;    /* advertising must be (re)started */
static bool started;

static ssize_t read_meas(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	uint8_t copy[BLE_MEAS_LEN];
	k_spinlock_key_t key = k_spin_lock(&lock);

	memcpy(copy, meas_val, sizeof(copy));
	k_spin_unlock(&lock, key);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, copy, sizeof(copy));
}

static ssize_t read_text(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			 void *buf, uint16_t len, uint16_t offset)
{
	char copy[BLE_TEXT_MAX + 1];
	size_t n;
	k_spinlock_key_t key = k_spin_lock(&lock);

	memcpy(copy, text_val, sizeof(copy));
	n = text_len;
	k_spin_unlock(&lock, key);
	return bt_gatt_attr_read(conn, attr, buf, len, offset, copy, (uint16_t)n);
}

static void meas_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	atomic_set(&meas_sub, (value & BT_GATT_CCC_NOTIFY) ? 1 : 0);
}

static void text_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	atomic_set(&text_sub, (value & BT_GATT_CCC_NOTIFY) ? 1 : 0);
}

/* attrs: [0] service, [1] meas declaration, [2] meas value, [3] CCC, [4] CUD,
 *        [5] text declaration, [6] text value, [7] CCC, [8] CUD */
BT_GATT_SERVICE_DEFINE(vib_svc,
	BT_GATT_PRIMARY_SERVICE(&svc_uuid),
	BT_GATT_CHARACTERISTIC(&meas_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_meas, NULL, NULL),
	BT_GATT_CCC(meas_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CUD("Vibration measurement", BT_GATT_PERM_READ),
	BT_GATT_CHARACTERISTIC(&text_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_text, NULL, NULL),
	BT_GATT_CCC(text_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CUD("Summary (text)", BT_GATT_PERM_READ),
);
#define MEAS_VALUE_ATTR (&vib_svc.attrs[2])
#define TEXT_VALUE_ATTR (&vib_svc.attrs[6])

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, VIB_SVC_UUID_VAL),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void connected(struct bt_conn *conn, uint8_t err)
{
	ARG_UNUSED(conn);
	if (err == 0) {
		atomic_inc(&conn_count);
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);
	atomic_dec(&conn_count);
	atomic_set(&meas_sub, 0);
	atomic_set(&text_sub, 0);
	atomic_set(&adv_needed, 1);    /* restarted from ble_vib_publish() */
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err == 0 || err == -EALREADY) {
		atomic_set(&adv_needed, 0);
		return 0;
	}
	return err;    /* e.g. connection object not freed yet: retried next block */
}

int ble_vib_init(void)
{
	int err;

	/* the notify calls below rely on these positions */
	if (bt_uuid_cmp(MEAS_VALUE_ATTR->uuid, &meas_uuid.uuid) != 0 ||
	    bt_uuid_cmp(TEXT_VALUE_ATTR->uuid, &text_uuid.uuid) != 0) {
		return -EINVAL;
	}
	err = bt_enable(NULL);
	if (err) {
		return err;
	}
	err = start_advertising();
	if (err) {
		return err;
	}
	started = true;
	return 0;
}

enum ble_vib_state ble_vib_publish(const struct vib_result *r, uint16_t seq)
{
	uint8_t m[BLE_MEAS_LEN];
	char t[BLE_TEXT_MAX + 1];
	size_t n;

	if (!started) {
		return BLE_VIB_OFF;
	}

	ble_meas_encode(r, seq, m);
	n = ble_text_format(r, t);

	k_spinlock_key_t key = k_spin_lock(&lock);

	memcpy(meas_val, m, sizeof(m));
	memcpy(text_val, t, sizeof(t));
	text_len = n;
	k_spin_unlock(&lock, key);

	if (atomic_get(&meas_sub)) {
		(void)bt_gatt_notify(NULL, MEAS_VALUE_ATTR, m, sizeof(m));
	}
	if (atomic_get(&text_sub)) {
		(void)bt_gatt_notify(NULL, TEXT_VALUE_ATTR, t, (uint16_t)n);
	}
	if (atomic_get(&adv_needed) && atomic_get(&conn_count) == 0) {
		(void)start_advertising();
	}

	if (atomic_get(&conn_count) <= 0) {
		return BLE_VIB_ADVERTISING;
	}
	return (atomic_get(&meas_sub) || atomic_get(&text_sub)) ? BLE_VIB_NOTIFYING : BLE_VIB_CONNECTED;
}

const char *ble_vib_state_str(enum ble_vib_state s)
{
	switch (s) {
	case BLE_VIB_ADVERTISING: return "advertising";
	case BLE_VIB_CONNECTED:   return "connected";
	case BLE_VIB_NOTIFYING:   return "notifying";
	default:                  return "off";
	}
}
