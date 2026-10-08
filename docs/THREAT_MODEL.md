# Threat model: Zephyr BLE vibration monitor (Nano 33 BLE)

Design-level analysis written by the author on 8 October 2026, using STRIDE on each trust boundary. It is a self-assessment, not an independent review. "Evidence" points to hardware results and tests already in this repository; items marked *analysis* are reasoning I have not tested.

## Scope and attackers
**In scope:** the BLE service, the pairing policy, the bond storage and the USB console commands.
**Out of scope:** the Arduino bootloader, the nRF52840's radio hardware, side channels, and the phone's own security.

| Attacker | Capability |
|---|---|
| A1 Nearby BLE attacker | Scans, connects, attempts to pair, sniffs and jams. Cannot see the USB console. |
| A2 Physical attacker | Holds the board: USB console, double-tap RESET bootloader, optionally a debug probe. |

## Assets
Vibration readings · pairing keys (bonds) in flash · the passkey shown during pairing · availability of the single BLE connection · firmware integrity.

## Data flow and trust boundaries
```
 iPhone <==== BLE (boundary B1, A1) ====> [ nRF52840: GATT service | pairing | settings in flash ]
                                                          ^
                          USB console and bootloader (boundary B2, A2): passkey, U-then-y command, firmware flashing
```

## Threats and mitigations
| ID | STRIDE | Threat | What stops it (evidence) | Residual risk |
|---|---|---|---|---|
| BL-01 | S, I | A stranger pairs and reads the data | Values and notification descriptors need an authenticated link; pairing needs a random passkey printed on the USB console; Secure Connections Only mode rejects "Just Works" pairing (checked in Zephyr's source). Evidence on hardware: pairing reaches security level 4, a wrong passkey fails with "authentication failed" and no notifications follow. | I never watched an unpaired phone attempt a direct read, and never tested a central that can only do Just Works. |
| BL-02 | S | A1 guesses the passkey | Six digits, a new random passkey for every pairing attempt (1 in 1,000,000 per attempt); the owner would see unexpected passkeys appear on the console. | No attempt limit or lockout in the application, and I have not examined whether the Zephyr stack throttles repeated failures. |
| BL-03 | T, I | Man in the middle during pairing | LE Secure Connections with passkey entry and enforced MITM protection (level 4). | Relies on the user typing the number shown on the board's own console. |
| BL-04 | I | A1 sniffs the notifications | The link is encrypted after pairing (security level 4 reached on hardware). | Advertising data is public (see BL-10). |
| BL-05 | I | Someone sees the passkey | It is printed only on the USB console; with no terminal open, nobody can read it and pairing cannot complete. | Anyone with the console open sees every pairing attempt's passkey: physical access is the trust anchor. |
| BL-06 | I | A2 extracts the bonding keys from flash | Not mitigated: bonds sit unencrypted in the storage partition; no flash encryption or read-out protection on this setup. | A cloned bond lets an attacker act as the paired phone. |
| BL-07 | T, D | A2 deletes the bonds | Command needs two keys within 5 seconds (state machine with 16 PC tests). | Anyone at the console can unpair every phone (accepted, physical trust). |
| BL-08 | T, E | A2 flashes different firmware | Not mitigated: no secure boot or signed images; the Arduino bootloader accepts any image after a double-tap RESET. | Out of scope here. The STM32 project implements secure boot; MCUboot on Zephyr is planned. |
| BL-09 | D | A stranger occupies the only connection | *Analysis:* not mitigated. The build has `CONFIG_BT_MAX_CONN=1`, and the application has no timeout for connections that never authenticate. | The legitimate phone cannot connect while a stranger holds the slot. Fix idea: drop links that have not reached security level 3 after a few seconds. |
| BL-10 | I | Tracking and fingerprinting | Not mitigated: Bluetooth privacy is off, so the public address, name and service UUID are advertised in clear. | Accepted; not a privacy-sensitive wearable. |
| BL-11 | T | A third phone evicts a paired phone | Needs the passkey from the console; at most two bonds, oldest replaced; unauthenticated overwrite is not enabled. | By design. |
| BL-12 | D | A1 jams the radio | Not mitigated. | Accepted. |
| BL-13 | R | No record of pairing attempts, failures or bond deletions | Console messages only, nothing stored. | Gap. A product needs a persistent security event log. |
| BL-14 | T | Compromised dependency or build | Zephyr and the SDK are pinned (CI uses the same versions); only the needed modules are fetched. | No software bill of materials, no signed firmware artifacts, no vulnerability-reporting process yet. |
| BL-15 | E | No privilege separation on the nRF52840 application | Not mitigated. | Accepted for a demonstrator. |

## Top residual risks
1. **No secure boot** (BL-08): the strongest gap for a product.
2. **Unauthenticated connection hogging** (BL-09): a small code change.
3. **Bonds in plain flash** (BL-06): enable read-out protection and encrypt stored keys.
