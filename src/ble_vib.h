/*
 * ble_vib.h - the vibration monitor's BLE peripheral: a custom GATT service
 * with two characteristics (binary measurement, text summary), both readable
 * and notifiable. Formats are in ble_fmt.h.
 */
#ifndef BLE_VIB_H
#define BLE_VIB_H

#include <stdint.h>
#include "vib_fft.h"

enum ble_vib_state {
	BLE_VIB_OFF,          /* Bluetooth not started */
	BLE_VIB_ADVERTISING,  /* waiting for a phone */
	BLE_VIB_CONNECTED,    /* connected, no notifications enabled */
	BLE_VIB_NOTIFYING,    /* connected and at least one characteristic subscribed */
};

/* Starts Bluetooth and advertising. Returns 0 or a negative error. */
int ble_vib_init(void);

/* Stores the latest result for reads, sends notifications to subscribers,
 * restarts advertising after a disconnection. Call once per analysed block. */
enum ble_vib_state ble_vib_publish(const struct vib_result *r, uint16_t seq);

const char *ble_vib_state_str(enum ble_vib_state s);

#endif
