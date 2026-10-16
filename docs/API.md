# Interfaces

What other code, or a person, can depend on in this repository: the six C modules in `src/`, the BLE service a phone sees, the helper tools, and the tests and CI that check them.
The design, the pairing security and the lessons learned are in the [README](../README.md) and [THREAT_MODEL.md](THREAT_MODEL.md); they are not repeated here.

The tables below are **generated** from the code by `python3 tools/make_api_docs.py` (between the `api:` markers; do not edit them by hand), and `tests/docs` fails if they fall
behind. The same tests check the numbers that the README and the other documents repeat (test counts, the example bytes, UUIDs, the sample rate, pinned versions, flash and
RAM percentages) against the code, the configuration and the CI files they come from.

## 1. How the code is divided

Four modules contain **no board code** (`accel_stats`, `vib_fft`, `ble_fmt`, `ble_cmd`): they are plain C, so they are tested on the PC (`tests/host`, and ztest on `native_sim`) before they
run on the board. The other two (`ble_vib`, `ble_sec`) use Zephyr's Bluetooth stack and can only run on the board. `src/main.c` ties them together: a sampler thread reads the
accelerometer every 2.5 ms into two ping-pong buffers; each full block of `VIB_N` samples goes through a message queue to an analysis thread, which calls `vib_fft_analyse`,
prints a line on the USB console and publishes the result over BLE.

Units: `accel_stats` works in integer milli-m/s^2 (1 g = 9807); `vib_fft` works in float m/s^2 and Hz; the BLE characteristics carry scaled 16-bit integers (see `ble_fmt.h` below).

## 2. C modules

<!-- api:c:start -->
#### `src/accel_stats.h` (pure C, tested on the PC)

integer statistics over a window of accelerometer samples.

```text
accel_stats.h - integer statistics over a window of accelerometer samples.

Units are milli-m/s^2 (1 g = 9807). Pure C, no Zephyr dependency, so it is
unit-tested on the PC (tests/host) before it runs on the board.
```

| Name | Declaration | What it does |
|---|---|---|
| `accel_isqrt64` | `uint32_t accel_isqrt64(uint64_t v)` | Integer square root: largest r with r*r <= v. |
| `accel_magnitude` | `int32_t accel_magnitude(const int32_t v[3])` | \|v\| in the same units, rounded down. |
| `accel_stats_reset` | `void accel_stats_reset(struct accel_stats *s)` | Start a new window: clear the sums and the count, and reset the magnitude range. |
| `accel_stats_add` | `void accel_stats_add(struct accel_stats *s, const int32_t v[3])` | Add one sample v (x, y, z): updates the per-axis sums, the count and the smallest and largest magnitude. |
| `accel_stats_mean` | `int accel_stats_mean(const struct accel_stats *s, int32_t mean[3])` | Per-axis mean (integer division, truncates toward zero). Returns -1 if empty. |

```c
struct accel_stats {
	int64_t sum[3];       /* per-axis sum of samples */
	uint32_t n;           /* number of samples */
	int32_t mag_min;      /* smallest per-sample magnitude */
	int32_t mag_max;      /* largest per-sample magnitude */
};
```

#### `src/vib_fft.h` (pure C, tested on the PC)

dominant vibration frequency from a block of 3-axis accelerometer samples, using CMSIS-DSP's real FFT.

```text
vib_fft.h - dominant vibration frequency from a block of 3-axis
accelerometer samples, using CMSIS-DSP's real FFT.

Per axis: remove the mean (gravity and offset), apply a Hann window,
real FFT; the power spectra of the three axes are summed, so the result does
not depend on how the board is oriented. The peak bin is refined by
Gaussian (log-parabolic) interpolation, and the amplitude is corrected for
the Hann window's scalloping loss.

Pure C + CMSIS-DSP: unit-tested on the PC (tests/host) with the same
CMSIS-DSP revision Zephyr uses. Not reentrant (static work buffers).
```

| Name | Declaration | What it does |
|---|---|---|
| `vib_fft_init` | `int vib_fft_init(void)` | Prepare the FFT and the Hann window. Call once before vib_fft_analyse(). Returns VIB_OK or VIB_ERR_INIT. |
| `vib_fft_analyse` | `int vib_fft_analyse(const float *const axis[3], uint32_t n, float fs_hz, float min_hz, float rms_threshold, struct vib_result *out)` | axis[0..2]: VIB_N samples each (m/s^2), fs_hz: the real sample rate, min_hz: ignore anything below (slow drift, tilting), rms_threshold: below this the board counts as still. |

| Constant | Value | Meaning |
|---|---|---|
| `VIB_N` | `512` | samples per block (power of two) |
| `VIB_BINS` | `(VIB_N / 2)` |  |
| `VIB_OK` | `0` |  |
| `VIB_ERR_INIT` | `-1` | vib_fft_init() not called or failed |
| `VIB_ERR_ARG` | `-2` | bad length, sample rate or frequency range |

```c
struct vib_result {
	float rms;          /* m/s^2, vibration only (mean removed), all axes together */
	float freq_hz;      /* dominant frequency */
	float amp;          /* peak amplitude of that component, m/s^2 */
	uint32_t peak_bin;  /* FFT bin of the peak before interpolation */
	bool active;        /* rms >= threshold: false means "still" */
};
```

#### `src/ble_fmt.h` (pure C, tested on the PC)

what the BLE characteristics carry, as pure C so it is tested on the PC (tests/host).

```text
ble_fmt.h - what the BLE characteristics carry, as pure C so it is tested
on the PC (tests/host). No Zephyr or Bluetooth code here.

Measurement characteristic, 9 bytes, little-endian:
  byte 0     flags   bit 0 = vibrating (0 = still)
  bytes 1-2  uint16  dominant frequency, 0.1 Hz units   (0 when still)
  bytes 3-4  uint16  amplitude, milli-m/s^2             (0 when still)
  bytes 5-6  uint16  rms, milli-m/s^2
  bytes 7-8  uint16  block sequence number (wraps)
Values are rounded and clamped to 0..65535.

Summary characteristic: UTF-8 text, at most 20 bytes so it fits one
notification at the default ATT MTU, e.g. "24.6Hz 0.51 0.37" or
"still rms 0.025".
```

| Name | Declaration | What it does |
|---|---|---|
| `ble_meas_encode` | `void ble_meas_encode(const struct vib_result *r, uint16_t seq, uint8_t out[BLE_MEAS_LEN])` | Encode r and the block sequence number seq into the 9-byte measurement; frequency and amplitude are 0 when r is not active. |
| `ble_text_format` | `size_t ble_text_format(const struct vib_result *r, char out[BLE_TEXT_MAX + 1])` | Writes the text (NUL-terminated) and returns its length (<= BLE_TEXT_MAX). |

| Constant | Value | Meaning |
|---|---|---|
| `BLE_MEAS_LEN` | `9` |  |
| `BLE_TEXT_MAX` | `20` |  |

#### `src/ble_cmd.h` (pure C, tested on the PC)

the two-key console command that forgets all paired phones.

```text
ble_cmd.h - the two-key console command that forgets all paired phones.

Pure C, no Zephyr: it only turns typed characters (and the passage of time)
into actions, so it can be tested on the PC.

  U          arms the command ("forget ALL paired phones?")
  Y          within 5 s: confirm -> BLE_CMD_UNPAIR
  any other key, or 5 s of silence: cancel
Carriage return / line feed are ignored, so "U", Enter, "Y" also works.
```

| Name | Declaration | What it does |
|---|---|---|
| `ble_cmd_init` | `void ble_cmd_init(struct ble_cmd *c)` | Start idle: not armed. |
| `ble_cmd_feed` | `enum ble_cmd_action ble_cmd_feed(struct ble_cmd *c, int ch, uint32_t now_ms)` | ch: the typed character, or -1 if none arrived. now_ms: any free-running millisecond counter (wrap-around at 2^32 is handled). Call it regularly, also with ch = -1, so a timeout is noticed. |

| Constant | Value | Meaning |
|---|---|---|
| `BLE_CMD_CONFIRM_MS` | `5000u` |  |

```c
enum ble_cmd_action {
	BLE_CMD_NONE,       /* nothing to do */
	BLE_CMD_ARMED,      /* tell the person to press Y to confirm */
	BLE_CMD_UNPAIR,     /* confirmed: forget all paired phones now */
	BLE_CMD_CANCELLED,  /* a wrong key while armed */
	BLE_CMD_TIMEOUT,    /* nobody confirmed in time */
};
```

```c
struct ble_cmd {
	bool armed;
	uint32_t armed_ms;
};
```

#### `src/ble_vib.h` (uses Zephyr, runs on the board only)

the vibration monitor's BLE peripheral: a custom GATT service with two characteristics (binary measurement, text summary), both readable and notifiable.

```text
ble_vib.h - the vibration monitor's BLE peripheral: a custom GATT service
with two characteristics (binary measurement, text summary), both readable
and notifiable. Formats are in ble_fmt.h.
```

| Name | Declaration | What it does |
|---|---|---|
| `ble_vib_init` | `int ble_vib_init(void)` | Starts Bluetooth and advertising. Returns 0 or a negative error. |
| `ble_vib_publish` | `enum ble_vib_state ble_vib_publish(const struct vib_result *r, uint16_t seq)` | Stores the latest result for reads, sends notifications to subscribers, restarts advertising after a disconnection. Call once per analysed block. |
| `ble_vib_state_str` | `const char *ble_vib_state_str(enum ble_vib_state s)` | Short text for a state, for the console log. Returns a constant string. |

```c
enum ble_vib_state {
	BLE_VIB_OFF,          /* Bluetooth not started */
	BLE_VIB_ADVERTISING,  /* waiting for a phone */
	BLE_VIB_CONNECTED,    /* connected, no notifications enabled */
	BLE_VIB_NOTIFYING,    /* connected and at least one characteristic subscribed */
};
```

#### `src/ble_sec.h` (uses Zephyr, runs on the board only)

the BLE pairing policy of the vibration monitor.

```text
ble_sec.h - the BLE pairing policy of the vibration monitor.

The Nano 33 BLE has no display and no keyboard, so the USB console is the
display: during pairing a random 6-digit passkey is printed there and the
person types it into the phone. Only a phone that did this ends up with an
authenticated, encrypted link (LE Secure Connections, security level 4).
The GATT attributes in ble_vib.c demand that level, so an unpaired phone
can connect but cannot read or subscribe to anything.

Step 8b: the pairing keys are stored in flash (Zephyr settings/NVS), so a
paired phone stays paired across a RESET. The USB console is also the
management interface: typing U then Y forgets every paired phone.
```

| Name | Declaration | What it does |
|---|---|---|
| `ble_sec_init` | `int ble_sec_init(void)` | Registers the pairing callbacks. Call once, before or after bt_enable(). Returns 0 or a negative error code. |
| `ble_sec_start` | `int ble_sec_start(void)` | Call after bt_enable() and settings_load(): reports how many paired phones were restored from flash and starts the console command (U, then Y) that forgets them all. Returns 0 or a negative error code. |
<!-- api:c:end -->

### 2.1 Examples

These are compiled with warnings as errors and run by `tests/docs`, against the real source files.

Average a few samples (a board lying flat reads about 1 g on z):

<!-- example-stats:start -->
```c
#include <stdint.h>
#include "accel_stats.h"

/* Two samples with x and y near zero and z near 1 g; units are milli-m/s^2. */
int32_t average_z(void)
{
	struct accel_stats s;
	const int32_t a[3] = { 10, -20, 9800 };
	const int32_t b[3] = { -10, 20, 9814 };
	int32_t mean[3];

	accel_stats_reset(&s);
	accel_stats_add(&s, a);
	accel_stats_add(&s, b);
	return accel_stats_mean(&s, mean) == 0 ? mean[2] : -1;   /* 9807 = 1 g */
}
```
<!-- example-stats:end -->

The console command that forgets all paired phones (type `U`, then `Y` within 5 seconds):

<!-- example-cmd:start -->
```c
#include <stdint.h>
#include "ble_cmd.h"

/* U at t = 1000 ms arms the command; Y two seconds later confirms it. */
int forget_is_confirmed(void)
{
	struct ble_cmd c;

	ble_cmd_init(&c);
	if (ble_cmd_feed(&c, 'U', 1000) != BLE_CMD_ARMED) {
		return 0;
	}
	return ble_cmd_feed(&c, 'Y', 3000) == BLE_CMD_UNPAIR;
}
```
<!-- example-cmd:end -->

## 3. The BLE service

<!-- api:gatt:start -->
The board advertises as **`VibMon Nano33`** (`CONFIG_BT_DEVICE_NAME` in `prj.conf`).

| Item | UUID | Properties | Permission needed | Defined as |
|---|---|---|---|---|
| Service | `5f2e0001-6d1b-4a3c-9b2e-7c4d1a2b3c4d` |  |  |  |
| Vibration measurement | `5f2e0002-6d1b-4a3c-9b2e-7c4d1a2b3c4d` | read, notify | read (authenticated link) | `meas_uuid` |
| Summary (text) | `5f2e0003-6d1b-4a3c-9b2e-7c4d1a2b3c4d` | read, notify | read (authenticated link) | `text_uuid` |
<!-- api:gatt:end -->

Both characteristics need an **authenticated** link (a paired phone); the names (the user descriptions) stay public. The byte layout of the measurement and the text of the summary are
defined in `ble_fmt.h` (shown above), and the README has a captured example that `tests/docs` decodes and re-encodes with the real code.

## 4. Tools

<!-- api:python:start -->
#### `tools/flash_map.py`

show where an image really sits in a flash dump.

| Function | Arguments | What it does |
|---|---|---|
| `read_file` | `(path)` | The whole file as bytes. |
| `words_at` | `(dump, addr)` | The two 32-bit little-endian words at addr in the dump (initial stack pointer, reset vector), or None if they lie outside it. |
| `describe_words` | `(w)` | Text saying what a pair of vector-table words looks like: erased, a plausible vector table, or neither. |
| `find_image` | `(dump, img, page)` | Every offset in the dump where the first 256 bytes of the image occur. |
| `build_map` | `(dump, page, img_start, img_len)` | The dump page by page, merged into runs: [first address, last address, (erased or data, image or other)]. |
| `main` | `(argv=None)` | Command line: map the dump, find the image in it and say whether it sits where the board expects. Returns the exit status. |

Command line:

| Option | What it does | Notes |
|---|---|---|
| `dump` | a flash read-back taken with bossac --read, starting at address 0 |  |
| `image` | the image to look for (zephyr.bin) |  |
| `--expect` | address the image should start at (default 0x10000) | default `0x10000` |
| `--page` | size of a flash page in bytes, for the map (default 4096) | default `4096` |
<!-- api:python:end -->

<!-- api:scripts:start -->
#### `tools/ci_setup.sh`

Sets up what the CI jobs (and anyone reproducing them) need, from scratch and repeatably.

```text
Sets up what the CI jobs (and anyone reproducing them) need, from scratch and
repeatably. Pinned: Zephyr v4.4.2, Zephyr SDK 1.0.1 (the versions this project
was developed with). Safe to run again: it skips what is already in place.

  tools/ci_setup.sh workspace   Zephyr source + only the modules this project needs
  tools/ci_setup.sh sdk         Zephyr SDK with the ARM toolchain (firmware build only)

Environment: CI_ROOT (default: current directory) is where zephyrproject/ and
zephyr-sdk-<version>/ are created.
```

#### `tools/flash_nano33ble.sh`

flash a Zephyr build to an Arduino Nano 33 BLE from WSL, using Arduino's own bossac.exe on Windows.

```text
tools/flash_nano33ble.sh - flash a Zephyr build to an Arduino Nano 33 BLE
from WSL, using Arduino's own bossac.exe on Windows.

  1. Double-tap RESET on the board (orange LED pulses slowly).
  2. powershell.exe -NoProfile -c "[System.IO.Ports.SerialPort]::GetPortNames()"
  3. sh tools/flash_nano33ble.sh COM7 [build_dir]     (default ~/build/vib)

NO --offset IS PASSED, on purpose. Arduino's build of bossac (1.9.1-arduino2)
writes at the bootloader's application address, 0x10000, by itself: Arduino's
own core links sketches at 0x10000 and uploads them with no offset
(ArduinoCore-mbed: variants/ARDUINO_NANO33BLE/linker_script.ld, platform.txt).
Zephyr's west runner adds "-o 0x10000" on top, which put the image at 0x20000
on this board, so the bootloader started the old Arduino sketch instead.
The Zephyr image is linked for 0x10000 (code partition), so the same call the
Arduino IDE makes is the right one.

Guard rails:
  - refuses an image bigger than the code partition (0xE8000 bytes)
  - refuses to flash unless bossac reports an nRF52840 with the Arduino
    bootloader on that port
  - checks bossac's exit status and the "Verify successful" line
```
<!-- api:scripts:end -->

## 5. Tests and CI

```text
make -C tests/host                          # plain C tests (the FFT one needs Zephyr's CMSIS-DSP: CMSIS_DSP=...)
west build -b native_sim/native/64 tests/unit && build/zephyr/zephyr.exe     # ztest suites (see the README)
python3 -m unittest discover -s tests/docs  # this reference and the README's numbers (Python only, no Zephyr)
```

<!-- api:tests:start -->
**ztest suites** (`tests/unit`, built for `native_sim`; this is what CI runs):

| Suite | File | Test cases |
|---|---|---|
| `accel_stats` | `tests/unit/src/test_accel_stats.c` | 10 |
| `ble_cmd` | `tests/unit/src/test_ble_cmd.c` | 13 |
| `ble_fmt` | `tests/unit/src/test_ble_fmt.c` | 8 |
| `vib_fft_1_before_init` | `tests/unit/src/test_vib_fft.c` | 1 |
| `vib_fft_2_analysis` | `tests/unit/src/test_vib_fft.c` | 12 |
| **5 suites** | | **44** |

**Plain C tests** (`tests/host`, `make -C tests/host`; the FFT one needs Zephyr's CMSIS-DSP source):

| Program | What it checks | Checks |
|---|---|---|
| `tests/host/test_accel_stats.c` | PC tests for src/accel_stats.c make -C tests/host | 14 |
| `tests/host/test_ble_cmd.c` | PC tests for src/ble_cmd.c | 16 |
| `tests/host/test_ble_fmt.c` | PC tests for src/ble_fmt.c | 10 |
| `tests/host/test_vib_fft.c` | PC tests for src/vib_fft.c, built against the same CMSIS-DSP revision Zephyr 4.4.2 uses (see tests/host/Makefile). | 17 |
<!-- api:tests:end -->

What `tests/docs` checks (this list is generated from the test file):

<!-- api:doctests:start -->
- The README's '44 test cases' (everywhere it says it) and 'five suites' equal the ZTEST cases and ZTEST_SUITEs in tests/unit.
- The README and the threat model's counts of PC tests for the FFT (17), ble_fmt (10) and ble_cmd (16) equal the CHECK calls in tests/host, and the three tests that need no CMSIS-DSP really print that many [PASS] lines.
- The captured measurement in the README (01 54 00 6D 21 E9 18 D0 00) decodes to the frequency, amplitude, rms and block it states, and the real ble_meas_encode produces exactly those bytes.
- The README's and ble_fmt.h's example texts ('24.6Hz 0.51 0.37' vibrating, 'still rms 0.025' still) are exactly what ble_text_format writes, and fit in 20 bytes.
- The README's service and characteristic UUIDs, properties and device name equal what src/ble_vib.c and prj.conf define.
- VIB_N, the 2.5 ms sample period and Zephyr's rounding of a timer period to 32768 Hz ticks give the README's 512 samples, 1.28 s, 399.6 Hz, 0.78 Hz bins and '50 Hz reads as 50.05 Hz'.
- BLE_CMD_CONFIRM_MS is 5000, and ble_cmd.h, ble_cmd.c, the README and the threat model all say 5 seconds.
- prj.conf has the settings the README describes (Secure Connections only, bonds in flash, two paired phones, the device name), and ble_sec.c's build check names the three settings the README says it guards.
- The README's '279,868 bytes of flash (29.45 % of 928 KB)' and '69,638 bytes of RAM (26.56 % of 256 KB)' are arithmetically consistent.
- Zephyr v4.4.2 and SDK 1.0.1 are the same in the README, ci.yml and tools/ci_setup.sh; the SDK checksum is a 64-digit SHA-256; and 'seven Zephyr modules' is the number in ci_setup.sh.
- The README's '15 threats' equals the number of distinct threat rows in docs/THREAT_MODEL.md.
- The tables in docs/API.md are exactly what tools/make_api_docs.py writes from the code today.
- Every .h file in src/ is documented, has a one-line summary, and every function declaration has a comment directly above it.
- A second, simpler reading of each header finds the same function names, #define constants and struct/enum names as appear in the generated tables.
- Every .c file in src/ and every tool appears in the README's layout block, and every path in that block exists.
- Every file or folder named in backticks in the README and the documents exists (build outputs, home-folder and Windows paths excepted).
- tools/flash_map.py has a docstring on every public function and help text on every option.
- Every script in tools/, every plain-C test program and every ztest suite has an entry in API.md.
- The two C examples in API.md compile with warnings as errors against the real headers and, linked with the real accel_stats.c and ble_cmd.c, return 9807 (1 g) and 'confirmed'.
- The README says how many jobs ci.yml has, in both places, and its job table lists exactly the jobs' names; the docs job runs these tests.
- Every tests/host/test_*.c has its compiled program listed in .gitignore, __pycache__ is ignored, and git tracks no bytecode.
<!-- api:doctests:end -->

## 6. Limits of this reference

- The ztest and FFT host tests run in CI (they need Zephyr); `tests/docs` does not run them. It counts their test cases and checks the README against those counts, and it runs the
  three host tests that need no CMSIS-DSP to confirm their counts.
- The flash and RAM sizes in the README come from the firmware build in CI. `tests/docs` checks that the percentages follow from the byte counts, not the byte counts themselves.
- Pairing, the sensor and the radio need the board and a phone; they were checked by hand (see the README's "BLE security").
- The examples run on the PC, built from the real sources.
