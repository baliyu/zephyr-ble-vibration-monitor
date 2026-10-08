# Security policy

`zephyr-ble-vibration-monitor` is a personal engineering project (a demonstrator), not a commercial product. I take security reports seriously and handle them on a best-effort basis.

## Supported versions
Only the latest commit on `main` is maintained.

## Reporting a vulnerability
Please do **not** open a public issue for a security problem.

1. Preferred: use GitHub's private vulnerability reporting (Security tab, then "Report a vulnerability").
2. Or email baliyu70@gmail.com with the subject `SECURITY: zephyr-ble-vibration-monitor`.

Please include what you found, the affected files or commit, how to reproduce it (including any hardware or phone you used), the impact you see, and whether you plan to publish.

## What to expect
- I aim to acknowledge a report within 7 days. I work alone, so this is a target, not a guarantee.
- I will tell you whether I can reproduce it and agree a disclosure date with you (coordinated disclosure). I aim to fix or document the problem within 90 days.
- Once a fix exists I publish a GitHub security advisory with a description, the affected commits, the severity and what to do, and credit you if you wish.

## In scope
- Any way for an unpaired or wrongly paired device to read or subscribe to the measurement or summary values.
- A bypass of the pairing policy (for example Just Works pairing being accepted, or a predictable passkey).
- Flaws in how bonds are stored, restored or deleted, including the `U` then `y` console command.
- Crashes or hangs that can be triggered over BLE.
- Weaknesses in the build or CI setup that could alter the firmware.

## Out of scope
- Physical attacks on the board (reading flash, flashing other firmware through the Arduino bootloader): a documented limitation.
- Vulnerabilities in Zephyr, mbedTLS or CMSIS themselves: please report those to the upstream projects.
- Denial of service by radio jamming.

## Known limitations
The residual risks I already know about are listed in [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md). A report that only restates one of them is welcome as a discussion but is not a new vulnerability.
