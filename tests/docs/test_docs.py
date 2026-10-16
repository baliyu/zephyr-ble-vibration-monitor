"""tests/docs/test_docs.py - keeps docs/API.md and the README honest: every number they repeat is checked against the code, the configuration or the CI files it comes from.

    python3 -m unittest discover -s tests/docs -v

Standard library only, plus gcc for the tests that compile the board-independent C modules. It needs no Zephyr.
"""
import ast
import math
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import make_api_docs as gen  # noqa: E402

DOCS = ["README.md", "SECURITY.md", "docs/API.md", "docs/CRA_MAPPING.md", "docs/THREAT_MODEL.md"]
ZEPHYR_NAMES = {"arduino_nano_33_ble/nrf52840", "smp.c"}
NUMBER_WORDS = {"one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8, "nine": 9, "ten": 10}
GCC = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-I", "src"]


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def run(cmd, cwd=ROOT):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def readme_section(title):
    text = read("README.md")
    a = text.index(f"## {title}")
    nxt = text.find("\n## ", a + 3)
    return text[a:nxt if nxt != -1 else len(text)]


def every(text, pattern, expected, where):
    """Every match of pattern in text (at least one) has the expected value (a string, or a tuple for several groups): a number is checked wherever it is repeated."""
    found = [m.groups() if len(m.groups()) > 1 else m.group(1) for m in re.finditer(pattern, text)]
    assert found, f"{where}: nothing matches {pattern!r}"
    bad = [f for f in found if f != expected]
    assert not bad, f"{where}: found {bad}, expected {expected}"


def build(tmp, sources, driver_code, extra=()):
    """Compile driver_code (a main) with the given sources from src/ and return the path of the program."""
    d = Path(tmp)
    (d / "driver.c").write_text(driver_code, encoding="utf-8")
    exe = d / "prog"
    r = run(GCC + ["-o", str(exe), str(d / "driver.c")] + [f"src/{s}" for s in sources] + list(extra) + ["-lm"])
    assert r.returncode == 0, r.stderr
    return exe


ENCODE = r'''
#include <stdio.h>
#include <stdlib.h>
#include "ble_fmt.h"
int main(int argc, char **argv)
{
	struct vib_result r = {0};
	uint8_t b[BLE_MEAS_LEN];
	char t[BLE_TEXT_MAX + 1];
	int i;
	(void)argc;
	r.active = atoi(argv[1]);
	r.freq_hz = (float)atof(argv[2]);
	r.amp = (float)atof(argv[3]);
	r.rms = (float)atof(argv[4]);
	ble_meas_encode(&r, (uint16_t)atoi(argv[5]), b);
	(void)ble_text_format(&r, t);
	for (i = 0; i < BLE_MEAS_LEN; i++) printf("%02X ", b[i]);
	printf("\n%s\n", t);
	return 0;
}
'''


class Numbers(unittest.TestCase):
    """Numbers the README and the documents repeat must be the numbers in the code and the configuration."""

    def test_the_ztest_counts_in_the_readme_are_the_real_counts(self):
        """The README's '44 test cases' (everywhere it says it) and 'five suites' equal the ZTEST cases and ZTEST_SUITEs in tests/unit."""
        suites = gen.ztest_suites()
        total = sum(n for _, _, n in suites)
        found = [int(x) for x in re.findall(r"(\d+) (?:ztest )?test cases", read("README.md"))]
        self.assertTrue(found)
        self.assertEqual(set(found), {total})
        words = re.search(r"test cases in (\w+) suites", read("README.md")).group(1)
        self.assertEqual(NUMBER_WORDS[words], len(suites))
        self.assertEqual(sum(len(re.findall(r"^ZTEST\(", p.read_text(encoding="utf-8"), re.M)) for p in (ROOT / "tests" / "unit" / "src").glob("test_*.c")), total)

    def test_the_pc_test_counts_in_the_readme_and_the_threat_model_are_the_real_counts(self):
        """The README and the threat model's counts of PC tests for the FFT (17), ble_fmt (10) and ble_cmd (16) equal the CHECK calls in tests/host, and the three tests that need no CMSIS-DSP really print that many [PASS] lines."""
        readme, threat = read("README.md"), read("docs/THREAT_MODEL.md")
        stated = {"vib_fft": int(re.search(r"(\d+) tests for the FFT module", readme).group(1)),
                  "ble_fmt": int(re.search(r"ble_fmt\.c`, which has no Zephyr code in it and (\d+) PC tests", readme).group(1)),
                  "ble_cmd": int(re.search(r"ble_cmd\.c`\) is a small state machine with (\d+) PC tests", readme).group(1))}
        self.assertEqual(int(re.search(r"state machine with (\d+) PC tests", threat).group(1)), stated["ble_cmd"])
        for module, n in stated.items():
            self.assertEqual(gen.host_checks(f"tests/host/test_{module}.c"), n, f"README says {n} PC tests for {module}")
        with tempfile.TemporaryDirectory() as d:
            for module, extra in (("accel_stats", []), ("ble_fmt", []), ("ble_cmd", [])):
                exe = Path(d) / module
                r = run(GCC + ["-o", str(exe), f"tests/host/test_{module}.c", f"src/{module}.c", "-lm"])
                self.assertEqual(r.returncode, 0, r.stderr)
                out = run([str(exe)])
                self.assertEqual(out.returncode, 0, out.stdout[-400:])
                self.assertEqual(out.stdout.count("[PASS]"), gen.host_checks(f"tests/host/test_{module}.c"), module)
                self.assertEqual(out.stdout.count("[FAIL]"), 0)

    def test_the_example_bytes_in_the_readme_decode_to_the_stated_values_and_the_real_encoder_makes_them_again(self):
        """The captured measurement in the README (01 54 00 6D 21 E9 18 D0 00) decodes to the frequency, amplitude, rms and block it states, and the real ble_meas_encode produces exactly those bytes."""
        text = read("README.md")
        raw = re.search(r"Example from a real capture: `([0-9A-F ]{26})`", text).group(1)
        b = bytes.fromhex(raw)
        self.assertEqual(len(b), 9)
        flags, freq, amp, rms, seq = b[0], int.from_bytes(b[1:3], "little"), int.from_bytes(b[3:5], "little"), int.from_bytes(b[5:7], "little"), int.from_bytes(b[7:9], "little")
        said = re.search(r"is vibrating, 0x([0-9A-F]+) = ([\d.]+) Hz, amplitude 0x([0-9A-F]+) = ([\d.]+) m/s\^2, rms 0x([0-9A-F]+) = ([\d.]+) m/s\^2, block (\d+)", text)
        self.assertEqual((flags & 1, int(said.group(1), 16), float(said.group(2)), int(said.group(3), 16), float(said.group(4)), int(said.group(5), 16), float(said.group(6)), int(said.group(7))),
                         (1, freq, freq / 10, amp, amp / 1000, rms, rms / 1000, seq))
        with tempfile.TemporaryDirectory() as d:
            exe = build(d, ["ble_fmt.c"], ENCODE)
            out = run([str(exe), "1", str(freq / 10), str(amp / 1000), str(rms / 1000), str(seq)]).stdout.split("\n")
        self.assertEqual(out[0].strip(), raw)

    def test_the_summary_text_examples_are_what_the_real_formatter_produces(self):
        """The README's and ble_fmt.h's example texts ('24.6Hz 0.51 0.37' vibrating, 'still rms 0.025' still) are exactly what ble_text_format writes, and fit in 20 bytes."""
        readme, header = read("README.md"), read("src/ble_fmt.h")
        for name, text in (("README.md", readme), ("ble_fmt.h", header)):
            self.assertIn("24.6Hz 0.51 0.37", text, name)
            self.assertIn("still rms 0.025", text, name)
        with tempfile.TemporaryDirectory() as d:
            exe = build(d, ["ble_fmt.c"], ENCODE)
            moving = run([str(exe), "1", "24.6", "0.51", "0.37", "1"]).stdout.split("\n")[1]
            still = run([str(exe), "0", "0", "0", "0.025", "1"]).stdout.split("\n")[1]
        self.assertEqual((moving, still), ("24.6Hz 0.51 0.37", "still rms 0.025"))
        self.assertTrue(max(len(moving), len(still)) <= 20)

    def test_the_uuids_and_the_device_name_in_the_readme_are_the_firmwares(self):
        """The README's service and characteristic UUIDs, properties and device name equal what src/ble_vib.c and prj.conf define."""
        g = gen.gatt()
        text = readme_section("BLE service")
        rows = {}
        for line in text.split("\n"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if cells[0] in ("Service", "Measurement", "Summary") and len(cells) >= 3:
                rows[cells[0]] = (cells[1].strip("`"), cells[2])
        self.assertEqual(set(rows), {"Service", "Measurement", "Summary"}, "the README's service table has changed shape")
        self.assertEqual([rows[k][0] for k in ("Service", "Measurement", "Summary")], [r[1] for r in g["rows"]])
        for key, row in (("Measurement", g["rows"][1]), ("Summary", g["rows"][2])):
            self.assertEqual(rows[key][1], row[2], f"{key}: properties")
        self.assertIn(f"`{g['name']}`", text)
        self.assertEqual(len({r[1][:8] for r in g["rows"]}), 3, "the three UUIDs differ only in the first group")

    def test_the_block_length_sample_rate_and_resolution_follow_from_the_constants(self):
        """VIB_N, the 2.5 ms sample period and Zephyr's rounding of a timer period to 32768 Hz ticks give the README's 512 samples, 1.28 s, 399.6 Hz, 0.78 Hz bins and '50 Hz reads as 50.05 Hz'."""
        n = int(next(v for c, v, _ in gen.parse_header(read("src/vib_fft.h"))["constants"] if c == "VIB_N").split()[0])
        period_us = int(re.search(r"#define SAMPLE_PERIOD_US\s+(\d+)", read("src/main.c")).group(1))
        ticks = math.ceil(period_us * 32768 / 1_000_000)       # Zephyr rounds a K_USEC timer period up to whole system-clock ticks
        measured = 32768 / ticks
        self.assertEqual((n, period_us), (512, 2500))
        every(read("README.md"), r"(\d+) samples = ([\d.]+) s", (str(n), f"{n * period_us / 1e6:.2f}"), "README: block length")
        every(read("README.md"), r"\(([\d.]+) Hz measured\)", f"{measured:.1f}", "README: measured rate in the diagram")
        every(read("README.md"), r"really gives ([\d.]+) Hz", f"{measured:.1f}", "README: measured rate")
        every(read("README.md"), r"50 Hz at ([\d.]+) Hz", f"{50 * 400 / measured:.2f}", "README: error when assuming 400 Hz")
        every(read("README.md"), r"bin width ([\d.]+) Hz", f"{measured / n:.2f}", "README: bin width")
        every(read("src/main.c"), r"resolution ([\d.]+) Hz", f"{measured / n:.2f}", "main.c: resolution")

    def test_the_forget_command_window_is_five_seconds_in_the_code_the_readme_and_the_threat_model(self):
        """BLE_CMD_CONFIRM_MS is 5000, and ble_cmd.h, ble_cmd.c, the README and the threat model all say 5 seconds."""
        header = read("src/ble_cmd.h")
        self.assertEqual(int(re.search(r"#define BLE_CMD_CONFIRM_MS (\d+)u", header).group(1)), 5000)
        every(header, r"within (\d+) s", "5", "ble_cmd.h")
        every(read("src/ble_cmd.c"), r"(\d+) s window", "5", "ble_cmd.c")
        every(read("README.md"), r"within (\d+) seconds", "5", "README")
        every(read("docs/THREAT_MODEL.md"), r"within (\d+) seconds", "5", "threat model")

    def test_the_security_settings_the_readme_names_are_in_prj_conf_and_checked_by_ble_sec_c(self):
        """prj.conf has the settings the README describes (Secure Connections only, bonds in flash, two paired phones, the device name), and ble_sec.c's build check names the three settings the README says it guards."""
        conf, readme, sec = read("prj.conf"), readme_section("BLE security"), read("src/ble_sec.c")
        for line in ("CONFIG_BT_SMP_SC_ONLY=y", "CONFIG_BT_SETTINGS=y", "CONFIG_BT_MAX_PAIRED=2"):
            self.assertIn(line, conf)
        self.assertIn("CONFIG_BT_SMP_SC_ONLY=y", readme)
        self.assertRegex(readme, r"Up to two phones can be paired")
        for setting in ("CONFIG_BT_SMP_SC_ONLY", "CONFIG_BT_SETTINGS", "CONFIG_BT_FIXED_PASSKEY"):
            self.assertIn(setting, readme)
            self.assertIn(setting, sec)
        self.assertNotIn("CONFIG_BT_FIXED_PASSKEY=y", conf)

    def test_the_flash_and_ram_percentages_in_the_readme_follow_from_the_byte_counts(self):
        """The README's '279,868 bytes of flash (29.45 % of 928 KB)' and '69,638 bytes of RAM (26.56 % of 256 KB)' are arithmetically consistent."""
        m = re.search(r"uses ([\d,]+) bytes of flash \(([\d.]+) % of (\d+) KB\) and ([\d,]+) bytes of RAM \(([\d.]+) % of (\d+) KB\)", read("README.md"))
        self.assertIsNotNone(m, "the README's flash and RAM sentence has changed shape")
        flash, fpct, fkb, ram, rpct, rkb = int(m.group(1).replace(",", "")), float(m.group(2)), int(m.group(3)), int(m.group(4).replace(",", "")), float(m.group(5)), int(m.group(6))
        self.assertEqual(round(flash / (fkb * 1024) * 100, 2), fpct)
        self.assertEqual(round(ram / (rkb * 1024) * 100, 2), rpct)

    def test_the_pinned_versions_and_module_count_agree_in_the_readme_the_workflow_and_the_setup_script(self):
        """Zephyr v4.4.2 and SDK 1.0.1 are the same in the README, ci.yml and tools/ci_setup.sh; the SDK checksum is a 64-digit SHA-256; and 'seven Zephyr modules' is the number in ci_setup.sh."""
        ci, setup, readme = read(".github/workflows/ci.yml"), read("tools/ci_setup.sh"), read("README.md")
        zephyr = re.search(r"ZEPHYR_VERSION: v([\d.]+)", ci).group(1)
        sdk = re.search(r'SDK_VERSION: "([\d.]+)"', ci).group(1)
        self.assertEqual(re.search(r'ZEPHYR_VERSION="\$\{ZEPHYR_VERSION:-v([\d.]+)\}"', setup).group(1), zephyr)
        self.assertEqual(re.search(r'SDK_VERSION="\$\{SDK_VERSION:-([\d.]+)\}"', setup).group(1), sdk)
        self.assertEqual(re.search(r"Zephyr RTOS ([\d.]+)", readme).group(1), zephyr)
        self.assertEqual(re.search(r"pinned to v([\d.]+)", readme).group(1), zephyr)
        both = re.search(r"Zephyr v([\d.]+) and SDK ([\d.]+)", readme)
        self.assertEqual((both.group(1), both.group(2)), (zephyr, sdk))
        self.assertRegex(re.search(r'SDK_MINIMAL_SHA256="(\w+)"', setup).group(1), r"^[0-9a-f]{64}$")
        modules = re.search(r'MODULES="([^"]+)"', setup).group(1).split()
        self.assertEqual(NUMBER_WORDS[re.search(r"only the (\w+) Zephyr modules", readme).group(1)], len(modules))

    def test_the_threat_count_in_the_readme_is_the_number_of_threats_in_the_threat_model(self):
        """The README's '15 threats' equals the number of distinct threat rows in docs/THREAT_MODEL.md."""
        stated = int(re.search(r"(\d+) threats", read("README.md")).group(1))
        self.assertEqual(stated, len(set(re.findall(r"^\| ([A-Z]{2}-\d+) \|", read("docs/THREAT_MODEL.md"), re.M))))


class Documentation(unittest.TestCase):
    """docs/API.md, the README's layout and the headers."""

    def test_the_generated_parts_of_api_md_are_current(self):
        """The tables in docs/API.md are exactly what tools/make_api_docs.py writes from the code today."""
        r = run([sys.executable, "tools/make_api_docs.py", "--check"])
        self.assertEqual(r.returncode, 0, r.stderr + r.stdout)

    def test_every_header_is_in_the_reference_and_every_declaration_has_a_comment(self):
        """Every .h file in src/ is documented, has a one-line summary, and every function declaration has a comment directly above it."""
        self.assertEqual({p.relative_to(ROOT).as_posix() for p in (ROOT / "src").glob("*.h")}, set(gen.HEADERS))
        api = read("docs/API.md")
        for rel in gen.HEADERS:
            info = gen.parse_header(read(rel))
            self.assertIn(f"#### `{rel}`", api)
            self.assertTrue(info["summary"], f"{rel}: the first line should read '/* name.h - summary'")
            for name, decl, comment in info["functions"]:
                self.assertTrue(comment, f"{rel}: '{name}' has no comment: {decl}")

    def test_every_function_constant_and_type_in_the_headers_appears_in_the_reference(self):
        """A second, simpler reading of each header finds the same function names, #define constants and struct/enum names as appear in the generated tables."""
        api = read("docs/API.md")
        for rel in gen.HEADERS:
            text = read(rel)
            consts = [m.group(1) for m in re.finditer(r"^#define[ \t]+(\w+)[ \t]+\S", text, re.M)]
            funcs = [m.group(1) for m in re.finditer(r"^[a-z][\w \t*]*?\b(\w+)\(", text, re.M)]
            types = re.findall(r"^(?:struct|enum) (\w+) \{", text, re.M)
            self.assertTrue(consts or funcs, rel)
            for name in consts + funcs:
                self.assertIn(f"`{name}`", api, f"{rel}: {name} is missing from docs/API.md")
            for t in types:
                self.assertIn(f" {t} {{", api, f"{rel}: type {t} is missing from docs/API.md")

    def test_the_readme_layout_lists_every_source_file_and_every_path_it_names_exists(self):
        """Every .c file in src/ and every tool appears in the README's layout block, and every path in that block exists."""
        layout = re.search(r"## Layout\n```\n(.*?)```", read("README.md"), re.S).group(1)
        paths = re.findall(r"^(\S+?)[ ,]", layout, re.M) + re.findall(r", (\S+)\s{2,}", layout)
        for p in paths:
            self.assertTrue((ROOT / p.rstrip("/")).exists(), f"the README's layout names {p}")
        for p in sorted((ROOT / "src").glob("*.c")) + sorted((ROOT / "tools").iterdir()):
            if p.suffix in (".c", ".sh", ".py") and p.name != "make_api_docs.py":
                self.assertIn(p.relative_to(ROOT).as_posix(), layout, f"{p.name} is missing from the README's layout")

    def test_every_file_path_mentioned_in_the_documents_exists(self):
        """Every file or folder named in backticks in the README and the documents exists (build outputs, home-folder and Windows paths excepted)."""
        paths = set()
        for dirpath, dirnames, files in os.walk(ROOT):
            dirnames[:] = [d for d in dirnames if d != ".git"]
            rel = Path(dirpath).relative_to(ROOT).as_posix()
            paths.add("" if rel == "." else rel)
            paths.update((Path(rel) / f).as_posix() if rel != "." else f for f in files)
        missing = []
        for doc in DOCS:
            for tok in set(re.findall(r"`([^`\n]+)`", read(doc))):
                tok = tok.strip()
                if not (re.fullmatch(r"[\w./-]+\.(c|h|py|sh|md|conf|overlay|txt|yml|ld)", tok) or re.fullmatch(r"[\w-]+/[\w./-]*", tok)):
                    continue
                if tok.startswith(("~", "/", "build/")) or re.search(r"\.(elf|hex|bin|map)$", tok) or "<" in tok or "*" in tok or tok.startswith("zephyr/"):
                    continue
                if tok in ZEPHYR_NAMES:                                 # a Zephyr board id and a Zephyr source file, not files of this repository
                    continue
                t = tok.rstrip("/")
                if not (t in paths or any(p.endswith("/" + t) for p in paths) or any(p.startswith(t + "/") for p in paths)):
                    missing.append(f"{doc}: {tok}")
        self.assertEqual(missing, [])

    def test_every_option_and_function_of_the_python_tool_is_documented(self):
        """tools/flash_map.py has a docstring on every public function and help text on every option."""
        for rel in gen.PYTHON_TOOLS:
            for name, _, doc in gen.public_functions(rel):
                self.assertTrue(doc.strip(), f"{rel}: {name} has no docstring")
            for names, help_text, _ in gen.argparse_rows(ast.parse(read(rel))):
                self.assertTrue(help_text, f"{rel}: option {names} has no help text")

    def test_every_tool_and_test_program_is_listed_in_the_reference(self):
        """Every script in tools/, every plain-C test program and every ztest suite has an entry in API.md."""
        api = read("docs/API.md")
        for p in (ROOT / "tools").iterdir():
            if p.suffix in (".sh", ".py") and p.name != "make_api_docs.py":
                self.assertIn(f"#### `tools/{p.name}`", api)
        for rel in gen.host_tests():
            self.assertIn(f"`{rel}`", api)
        for suite, _, _ in gen.ztest_suites():
            self.assertIn(f"`{suite}`", api)

    def test_the_c_examples_in_api_md_compile_and_give_the_stated_answers(self):
        """The two C examples in API.md compile with warnings as errors against the real headers and, linked with the real accel_stats.c and ble_cmd.c, return 9807 (1 g) and 'confirmed'."""
        api = read("docs/API.md")
        stats = re.search(r"<!-- example-stats:start -->\s*```c\n(.*?)```", api, re.S).group(1)
        cmd = re.search(r"<!-- example-cmd:start -->\s*```c\n(.*?)```", api, re.S).group(1)
        with tempfile.TemporaryDirectory() as d:
            exe = build(d, ["accel_stats.c"], stats + "\nint main(void) { return average_z() == 9807 ? 0 : 1; }\n")
            self.assertEqual(run([str(exe)]).returncode, 0, "average_z() did not return 9807")
            exe = build(d, ["ble_cmd.c"], cmd + "\nint main(void) { return forget_is_confirmed() ? 0 : 1; }\n")
            self.assertEqual(run([str(exe)]).returncode, 0, "forget_is_confirmed() did not return 1")


class Wiring(unittest.TestCase):
    """CI and housekeeping."""

    def test_the_readme_counts_and_names_the_ci_jobs_correctly(self):
        """The README says how many jobs ci.yml has, in both places, and its job table lists exactly the jobs' names; the docs job runs these tests."""
        ci = read(".github/workflows/ci.yml")
        jobs = re.findall(r"^  ([\w-]+):\n    name: (.+)$", ci[ci.index("\njobs:"):], re.M)
        self.assertEqual(len(jobs), 3)
        readme = read("README.md")
        self.assertEqual(NUMBER_WORDS[re.search(r"(\w+) jobs, (?:both|all) on Ubuntu", readme).group(1).lower()], len(jobs))
        self.assertEqual(NUMBER_WORDS[re.search(r"ci\.yml\s+the CI workflow \((\w+) jobs", readme).group(1)], len(jobs))
        table = re.findall(r"^\| \*\*(.+?)\*\* \|", readme_section("Continuous integration"), re.M)
        self.assertEqual(sorted(table), sorted(name for _, name in jobs))
        self.assertIn("python -m unittest discover -s tests/docs", ci)
        self.assertRegex(ci, r"(?m)^  pull_request:")
        self.assertRegex(ci, r"(?m)^  push:")

    def test_every_built_host_test_is_git_ignored_and_bytecode_is_not_committed(self):
        """Every tests/host/test_*.c has its compiled program listed in .gitignore, __pycache__ is ignored, and git tracks no bytecode."""
        ign = read(".gitignore")
        for rel in gen.host_tests():
            self.assertIn(f"tests/host/{Path(rel).stem}\n", ign, f"{rel}'s program is not in .gitignore")
        self.assertIn("__pycache__/", ign)
        r = run(["git", "ls-files"])
        if r.returncode == 0:
            tracked = [f for f in r.stdout.split("\n") if f and (ROOT / f).exists()]
            self.assertEqual([f for f in tracked if f.endswith(".pyc") or "__pycache__" in f], [])


if __name__ == "__main__":
    unittest.main()
