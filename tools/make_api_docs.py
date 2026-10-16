#!/usr/bin/env python3
"""tools/make_api_docs.py - write the generated parts of docs/API.md from the code.

  python3 tools/make_api_docs.py            rewrite docs/API.md
  python3 tools/make_api_docs.py --check    exit 1 if docs/API.md is out of date (what the tests and CI run)

Standard library only; no Zephyr needed. The headers in src/ are read as text, the BLE service from src/ble_vib.c and prj.conf, the Python tool with ast, the test suites
by counting their test cases, so no number in the reference is typed in. Everything between <!-- api:NAME:start --> and <!-- api:NAME:end --> is replaced; the rest is
written by hand.
"""
from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "docs" / "API.md"
HEADERS = ["src/accel_stats.h", "src/vib_fft.h", "src/ble_fmt.h", "src/ble_cmd.h", "src/ble_vib.h", "src/ble_sec.h"]
PURE = {"src/accel_stats.h", "src/vib_fft.h", "src/ble_fmt.h", "src/ble_cmd.h"}      # no board code: tested on the PC
SCRIPTS = ["tools/ci_setup.sh", "tools/flash_nano33ble.sh"]
PYTHON_TOOLS = ["tools/flash_map.py"]


# ---------------------------------------------------------------- helpers
def esc(s):
    return s.replace("|", "\\|")


def first_sentence(text):
    """The first sentence of a text (up to the first . ? or ! followed by a space or the end), or all of it."""
    m = re.search(r"[.?!](?=\s|$)", text)
    return text[:m.end()] if m else text


def first_paragraph(doc):
    return re.sub(r"\s+", " ", (doc or "").strip().split("\n\n")[0]).strip()


def strip_name(text):
    return re.sub(r"^[\w./-]+\s+-\s+", "", text)


def comment_text(lines):
    t = " ".join(lines)
    t = re.sub(r"/\*+|\*+/", " ", t)
    t = re.sub(r"(^|\s)\*+(?=\s|$)", " ", t)
    return re.sub(r"\s+", " ", t).strip()


def top_comment_raw(text):
    """The comment that opens a C file as lines (markers and the leading ' * ' removed), or []."""
    t = text.lstrip()
    if not t.startswith("/*") or "*/" not in t:
        return []
    body = t[:t.index("*/")].split("\n")
    out = [re.sub(r"^\s*(/\*+|\*)\s?", "", ln).rstrip() for ln in body]
    while out and not out[0].strip():
        out.pop(0)
    while out and not out[-1].strip():
        out.pop()
    return out


def top_comment(text):
    """The opening comment of a C file as one cleaned line, without the leading 'name - ' (empty if the file does not open that way)."""
    raw = top_comment_raw(text)
    m = re.match(r"^[\w/.]+\s+-\s+(.*)$", comment_text(raw)) if raw else None
    return m.group(1) if m else ""


def parse_header(text):
    """Summary, #define constants, struct/enum definitions and function declarations of a C header: a dict."""
    lines = text.split("\n")
    constants, functions = [], []
    for ln in lines:
        m = re.match(r"^#define[ \t]+([A-Za-z_]\w*)[ \t]+(.*)$", ln)
        if m:
            value, _, comment = m.group(2).partition("/*")
            if value.strip():
                constants.append((m.group(1), value.strip(), comment.replace("*/", "").strip()))
    types = [m.group(0) for m in re.finditer(r"^(?:struct|enum)\s+\w+\s*\{.*?^\};", text, re.S | re.M)]
    i = 0
    while i < len(lines):
        ln = lines[i]
        m = re.match(r'^(?!#|/\*|\*|\}|struct\s+\w+\s*\{|enum\s+\w+\s*\{)[A-Za-z_][\w \t*]*?\b(\w+)\s*\(', ln)
        if m:
            j = i
            while ";" not in lines[j]:
                j += 1
            decl = re.sub(r"\s+", " ", " ".join(x.strip() for x in lines[i:j + 1])).rstrip(";").strip()
            comment = ""
            if i > 0 and lines[i - 1].rstrip().endswith("*/"):
                k = i - 1
                while "/*" not in lines[k]:
                    k -= 1
                comment = comment_text(lines[k:i])
            functions.append((m.group(1), decl, comment))
            i = j
        i += 1
    return {"summary": first_sentence(top_comment(text)), "constants": constants, "types": types, "functions": functions}


def uuid_text(a, b, c, d, e):
    return f"{int(a, 16):08x}-{int(b, 16):04x}-{int(c, 16):04x}-{int(d, 16):04x}-{int(e, 16):012x}"


def gatt():
    """The BLE service as the firmware defines it: {'name', 'rows': [(item, uuid, properties, permission, label)]}."""
    src = (ROOT / "src" / "ble_vib.c").read_text(encoding="utf-8")
    uuids = {m.group(1): uuid_text(*m.groups()[1:]) for m in re.finditer(
        r"#define\s+VIB_(\w+)_UUID_VAL\s+BT_UUID_128_ENCODE\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\)", src)}
    chars = re.findall(r"BT_GATT_CHARACTERISTIC\(&(\w+)_uuid\.uuid,\s*([^,]+?),\s*(BT_GATT_PERM_\w+)", src)
    labels = re.findall(r'BT_GATT_CUD\("([^"]+)"', src)
    conf = (ROOT / "prj.conf").read_text(encoding="utf-8")
    rows = [("Service", uuids["SVC"], "", "", "")]
    for (var, props, perm), label, key in zip(chars, labels, ("MEAS", "TEXT")):
        names = ", ".join(p.strip().replace("BT_GATT_CHRC_", "").lower() for p in props.split("|"))
        perm_name = perm.replace("BT_GATT_PERM_", "").lower()
        rows.append((label, uuids[key], names, perm_name[:-len("_authen")] + " (authenticated link)" if perm_name.endswith("_authen") else perm_name, var))
    return {"name": re.search(r'CONFIG_BT_DEVICE_NAME="([^"]+)"', conf).group(1), "rows": rows}


def ztest_suites():
    """[(suite, file, number of ZTEST cases)] from tests/unit/src."""
    out = []
    for p in sorted((ROOT / "tests" / "unit" / "src").glob("test_*.c")):
        text = p.read_text(encoding="utf-8")
        counts = {}
        for m in re.finditer(r"^ZTEST\((\w+),", text, re.M):
            counts[m.group(1)] = counts.get(m.group(1), 0) + 1
        for m in re.finditer(r"^ZTEST_SUITE\((\w+),", text, re.M):
            out.append((m.group(1), f"tests/unit/src/{p.name}", counts.get(m.group(1), 0)))
    return sorted(out)


def host_checks(path):
    """The number of CHECK(...) calls in a plain-C host test (its #define does not count)."""
    return len(re.findall(r"^\s*CHECK\(", (ROOT / path).read_text(encoding="utf-8"), re.M))


def host_tests():
    return sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / "tests" / "host").glob("test_*.c"))


def public_functions(rel):
    tree = ast.parse((ROOT / rel).read_text(encoding="utf-8"))
    return [(n.name, f"({ast.unparse(n.args)})", ast.get_docstring(n) or "") for n in tree.body if isinstance(n, ast.FunctionDef) and not n.name.startswith("_")]


def argparse_rows(tree):
    rows = []
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute) and node.func.attr == "add_argument":
            names = [a.value for a in node.args if isinstance(a, ast.Constant) and isinstance(a.value, str)]
            kw = {k.arg: k.value for k in node.keywords}

            def lit(key):
                try:
                    return ast.literal_eval(kw[key]) if key in kw else None
                except ValueError:
                    return None
            extra = f"default `{lit('default')}`" if lit("default") is not None else ""
            rows.append((", ".join(f"`{n}`" for n in names), " ".join((lit("help") or "").split()), extra))
    return rows


# ---------------------------------------------------------------- the generated sections
def section_c():
    out = []
    for rel in HEADERS:
        text = (ROOT / rel).read_text(encoding="utf-8")
        info = parse_header(text)
        kind = "pure C, tested on the PC" if rel in PURE else "uses Zephyr, runs on the board only"
        out += [f"#### `{rel}` ({kind})", "", info["summary"], "", "```text", *top_comment_raw(text), "```", ""]
        if info["functions"]:
            out += ["| Name | Declaration | What it does |", "|---|---|---|"] + [f"| `{n}` | `{esc(d)}` | {esc(c)} |" for n, d, c in info["functions"]] + [""]
        if info["constants"]:
            out += ["| Constant | Value | Meaning |", "|---|---|---|"] + [f"| `{n}` | `{esc(v)}` | {esc(c)} |" for n, v, c in info["constants"]] + [""]
        for t in info["types"]:
            out += ["```c", t, "```", ""]
    return "\n".join(out).rstrip("\n")


def section_gatt():
    g = gatt()
    out = [f"The board advertises as **`{g['name']}`** (`CONFIG_BT_DEVICE_NAME` in `prj.conf`).", "",
           "| Item | UUID | Properties | Permission needed | Defined as |", "|---|---|---|---|---|"]
    out += [f"| {item} | `{uuid}` | {props} | {perm} | {('`' + var + '_uuid`') if var else ''} |" for item, uuid, props, perm, var in g["rows"]]
    return "\n".join(out)


def section_python():
    out = []
    for rel in PYTHON_TOOLS:
        tree = ast.parse((ROOT / rel).read_text(encoding="utf-8"))
        out += [f"#### `{rel}`", "", first_sentence(strip_name(first_paragraph(ast.get_docstring(tree)))), ""]
        out += ["| Function | Arguments | What it does |", "|---|---|---|"] + [f"| `{n}` | `{esc(s)}` | {esc(first_paragraph(d))} |" for n, s, d in public_functions(rel)] + [""]
        rows = argparse_rows(tree)
        out += ["Command line:", "", "| Option | What it does | Notes |", "|---|---|---|"] + [f"| {n} | {esc(h)} | {x} |" for n, h, x in rows]
    return "\n".join(out)


def section_scripts():
    out = []
    for rel in SCRIPTS:
        text = (ROOT / rel).read_text(encoding="utf-8")
        block = []
        for ln in text.split("\n")[1:]:
            if not ln.startswith("#"):
                break
            block.append(ln[2:] if ln.startswith("# ") else ln[1:])
        para = []
        for b in block:
            if not b.strip():
                break
            para.append(b.strip())
        out += [f"#### `{rel}`", "", first_sentence(strip_name(" ".join(para))), "", "```text", *block, "```", ""]
    return "\n".join(out).rstrip("\n")


def section_tests():
    out = ["**ztest suites** (`tests/unit`, built for `native_sim`; this is what CI runs):", "", "| Suite | File | Test cases |", "|---|---|---|"]
    suites = ztest_suites()
    out += [f"| `{s}` | `{f}` | {n} |" for s, f, n in suites]
    out += [f"| **{len(suites)} suites** | | **{sum(n for _, _, n in suites)}** |", "",
            "**Plain C tests** (`tests/host`, `make -C tests/host`; the FFT one needs Zephyr's CMSIS-DSP source):", "", "| Program | What it checks | Checks |", "|---|---|---|"]
    for rel in host_tests():
        text = (ROOT / rel).read_text(encoding="utf-8")
        out.append(f"| `{rel}` | {esc(first_sentence(top_comment(text)))} | {host_checks(rel)} |")
    return "\n".join(out)


def section_doctests():
    tree = ast.parse((ROOT / "tests" / "docs" / "test_docs.py").read_text(encoding="utf-8"))
    tests = sorted((n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name.startswith("test_")), key=lambda n: n.lineno)
    return "\n".join(f"- {first_paragraph(ast.get_docstring(n))}" for n in tests)


SECTIONS = {"c": section_c, "gatt": section_gatt, "python": section_python, "scripts": section_scripts, "tests": section_tests, "doctests": section_doctests}


def fill(doc):
    """docs/API.md with every generated section rewritten. Raises SystemExit if a marker is missing."""
    for name, fn in SECTIONS.items():
        start, end = f"<!-- api:{name}:start -->", f"<!-- api:{name}:end -->"
        if start not in doc or end not in doc:
            sys.exit(f"make_api_docs: {DOC} has no '{start}' ... '{end}' markers")
        a, b = doc.index(start) + len(start), doc.index(end)
        doc = doc[:a] + "\n" + fn() + "\n" + doc[b:]
    return doc


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--check", action="store_true", help="do not write; exit 1 if docs/API.md is out of date")
    a = ap.parse_args(argv)
    current = DOC.read_text(encoding="utf-8").replace("\r\n", "\n")
    new = fill(current)
    if a.check:
        if new != current:
            print("docs/API.md is out of date: run  python3 tools/make_api_docs.py", file=sys.stderr)
            return 1
        print("docs/API.md is up to date")
        return 0
    if new == current:
        print("docs/API.md already up to date")
    else:
        DOC.write_text(new, encoding="utf-8", newline="\n")
        print("docs/API.md updated")
    return 0


if __name__ == "__main__":
    sys.exit(main())
