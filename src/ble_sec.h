/*
 * ble_sec.h - the BLE pairing policy of the vibration monitor.
 *
 * The Nano 33 BLE has no display and no keyboard, so the USB console is the
 * display: during pairing a random 6-digit passkey is printed there and the
 * person types it into the phone. Only a phone that did this ends up with an
 * authenticated, encrypted link (LE Secure Connections, security level 4).
 * The GATT attributes in ble_vib.c demand that level, so an unpaired phone
 * can connect but cannot read or subscribe to anything.
 *
 * Step 8b: the pairing keys are stored in flash (Zephyr settings/NVS), so a
 * paired phone stays paired across a RESET. The USB console is also the
 * management interface: typing U then Y forgets every paired phone.
 */
#ifndef BLE_SEC_H
#define BLE_SEC_H

/* Registers the pairing callbacks. Call once, before or after bt_enable().
 * Returns 0 or a negative error code. */
int ble_sec_init(void);

/* Call after bt_enable() and settings_load(): reports how many paired phones
 * were restored from flash and starts the console command (U, then Y) that
 * forgets them all. Returns 0 or a negative error code. */
int ble_sec_start(void);

#endif
