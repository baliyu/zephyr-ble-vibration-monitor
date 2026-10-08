# CRA gap analysis: Zephyr BLE vibration monitor (Nano 33 BLE)

Self-assessment by the author, 8 October 2026, against Annex I of the EU Cyber Resilience Act (Regulation (EU) 2024/2847). **This repository is a demonstrator, not a product placed on the EU market, so the Act does not apply to it.** The exercise asks how it would measure up if it were a product. It is not legal advice and not a conformity assessment. Requirement wording is paraphrased; the exact text is in the Official Journal.

**Dates:** the reporting obligations (Article 14) apply from 11 September 2026; the rest of the Act, including all Annex I requirements and CE marking, applies from 11 December 2027.

**Reporting (Article 14), for a real product:** a manufacturer must notify actively exploited vulnerabilities and severe incidents through ENISA's single reporting platform: an early warning within 24 hours, a notification within 72 hours, and a final report within 14 days after a fix is available (vulnerabilities). The contact point for this repository is in `SECURITY.md`.

Status: **Met** / **Partly** (something required is missing) / **Not met** / **Not assessed** (I have not checked).

**Result:** 4 met, 12 partly, 4 not met, 2 not assessed, out of 22 requirements. Threat model: [THREAT_MODEL.md](THREAT_MODEL.md).

## Part I: properties of the product

| Annex I | Requirement (paraphrased) | Status | Evidence or gap |
|---|---|---|---|
| Part I, 1 | Appropriate cybersecurity level, based on the risks | **Partly** | A design-level threat model exists (`THREAT_MODEL.md`). It is a self-assessment, not the formal risk assessment the Act describes. |
| Part I, 2(a) | No known exploitable vulnerabilities when released | **Not assessed** | Zephyr 4.4.2 and its modules are pinned, but I have not checked them against published advisories. |
| Part I, 2(b) | Secure by default; the product can be reset to its original state | **Partly** | Secure by default: the data is unreadable until a phone pairs with a passkey, there is no default passkey, and Just Works pairing is rejected. Reset: `U` then `y` forgets all bonds, but there is no full factory reset. |
| Part I, 2(c) | Vulnerabilities can be fixed by security updates (automatic by default with opt-out, update notices, postponement) | **Not met** | No update mechanism. Firmware is changed by flashing over USB with the Arduino bootloader; images are not signed. |
| Part I, 2(d) | Protection from unauthorised access (authentication, access management) and reporting of possible unauthorised access | **Partly** | Authenticated LE Secure Connections pairing (security level 4) guards the values. Failed or unexpected pairing attempts are only printed on the console, not recorded. |
| Part I, 2(e) | Confidentiality of stored and transmitted data (state-of-the-art encryption) | **Partly** | The link is encrypted after pairing. The bonding keys sit unencrypted in flash. |
| Part I, 2(f) | Integrity of data, commands, programs and configuration; reporting of corruption | **Partly** | The link protects data in transit and build-time guards protect the security configuration. Program integrity is not protected (no secure boot) and corruption is not reported. |
| Part I, 2(g) | Data minimisation | **Met** | Only vibration values leave the device; no personal data is processed. |
| Part I, 2(h) | Availability of essential functions, also after an incident; resilience against denial of service | **Not met** | A stranger can occupy the only BLE connection (`CONFIG_BT_MAX_CONN=1`, no timeout for unauthenticated links); radio jamming is not addressed. |
| Part I, 2(i) | Limited negative impact on the availability of other devices and networks | **Met** | One small notification per 1.28 s block; the device does not relay or generate traffic for others. |
| Part I, 2(j) | Limited attack surface, including external interfaces | **Partly** | Only a BLE peripheral with one service and a USB console with a two-key command. Service structure and labels are visible before pairing. |
| Part I, 2(k) | Exploitation mitigation to reduce the impact of an incident | **Not assessed** | I have not reviewed Zephyr's exploit-mitigation options (stack protection, memory protection) for this build. |
| Part I, 2(l) | Security logging and monitoring of relevant internal activity (with user opt-out) | **Not met** | Pairing events are printed on the console only; nothing is stored. |
| Part I, 2(m) | Users can securely and easily remove all data and settings permanently | **Partly** | `U` then `y` deletes the bonds from RAM and flash. There is no function that erases all settings and data. |

## Part II: vulnerability handling

| Annex I | Requirement (paraphrased) | Status | Evidence or gap |
|---|---|---|---|
| Part II, (1) | Identify and document vulnerabilities and components, including a machine-readable SBOM (at least top-level dependencies) | **Partly** | The dependency set is pinned and listed (`tools/ci_setup.sh`, README). No SBOM file yet; Zephyr's `west spdx` command can produce one (the command exists; I have not run it here). |
| Part II, (2) | Fix vulnerabilities without delay, including by security updates | **Partly** | `SECURITY.md` commits to fixing reported issues, but there is no way to deliver a fix to a deployed board other than reflashing. |
| Part II, (3) | Effective and regular security tests and reviews | **Partly** | 44 ztest cases, plain C tests, CI on every push, hardware tests of the pairing flow, a threat model. No fuzzing, static analysis or independent review. |
| Part II, (4) | Publicly disclose fixed vulnerabilities once an update is available | **Partly** | `SECURITY.md` promises a GitHub security advisory once a fix exists; none has been needed yet. |
| Part II, (5) | A coordinated vulnerability disclosure policy | **Met** | `SECURITY.md` states the coordinated disclosure policy. |
| Part II, (6) | Facilitate reporting, including a contact address, for the product and its third-party components | **Met** | `SECURITY.md` gives a private reporting route and an email address; reports about Zephyr itself go to that project. |
| Part II, (7) | Mechanisms to distribute updates securely (automatic where applicable) | **Not met** | No secure update channel or signed images. |
| Part II, (8) | Disseminate updates without delay, free of charge, with advisory messages | **Partly** | The code is MIT-licensed and free; advisories would go through GitHub. Nothing pushes updates to a device. |

## The three gaps I would close first

1. **Updates and firmware integrity** (I.2c, I.2f, II.7): add signed images and a real update path, for example MCUboot on Zephyr.
1. **Availability of the single connection** (I.2h): drop connections that have not reached security level 3 within a few seconds.
1. **Security event log** (I.2l): keep pairing failures and bond deletions in non-volatile storage.
