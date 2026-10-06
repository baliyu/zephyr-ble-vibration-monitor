#!/usr/bin/env python3
"""tools/flash_map.py - show where an image really sits in a flash dump.

    python3 tools/flash_map.py dump.bin zephyr.bin [--expect 0x10000] [--page 4096]

dump.bin is a flash read-back taken with bossac (--read), starting at flash
address 0. The tool
  * maps the dump page by page (erased / data, image / other),
  * finds the image by matching its first bytes, and checks the whole image
    is present, unchanged, at that address,
  * prints the first vector-table words (initial stack pointer, reset vector)
    at the interesting start addresses 0x10000, 0x16000 and 0x20000,
  * says whether the image is where the board's devicetree expects it.
It only reads files; it never touches the board.
"""
import argparse
import struct
import sys


def read_file(path):
    with open(path, "rb") as f:
        return f.read()


def words_at(dump, addr):
    if addr + 8 > len(dump):
        return None
    return struct.unpack_from("<II", dump, addr)


def describe_words(w):
    if w is None:
        return "(outside the dump)"
    sp, reset = w
    if w == (0xFFFFFFFF, 0xFFFFFFFF):
        return "erased"
    sp_ok = 0x20000000 <= sp <= 0x20040000
    return "SP=0x%08X reset=0x%08X  %s" % (sp, reset, "plausible vector table" if sp_ok else "not a vector table")


def find_image(dump, img, page):
    probe = img[:256]
    hits = []
    start = 0
    while True:
        pos = dump.find(probe, start)
        if pos < 0:
            break
        hits.append(pos)
        start = pos + 1
    return hits


def build_map(dump, page, img_start, img_len):
    rows = []
    for a in range(0, len(dump), page):
        p = dump[a:a + page]
        erased = p.count(0xFF) == len(p)
        in_img = img_start is not None and img_start <= a < img_start + img_len
        key = ("erased" if erased else "data", "image" if in_img else "other")
        if rows and rows[-1][2] == key:
            rows[-1][1] = a + page - 1
        else:
            rows.append([a, a + page - 1, key])
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dump")
    ap.add_argument("image")
    ap.add_argument("--expect", default="0x10000", help="address the image should start at (default 0x10000)")
    ap.add_argument("--page", type=int, default=4096)
    a = ap.parse_args(argv)

    dump, img = read_file(a.dump), read_file(a.image)
    expect = int(a.expect, 0)
    print("dump: %d bytes (0x%X), image: %d bytes" % (len(dump), len(dump), len(img)))
    if len(img) < 64:
        print("image is too small to match reliably")
        return 2

    hits = find_image(dump, img, a.page)
    img_start = None
    for h in hits:
        if dump[h:h + len(img)] == img:
            img_start = h
            break
    partial = hits[0] if (hits and img_start is None) else None

    print("\nflash map (page = %d bytes):" % a.page)
    for lo, hi, (state, owner) in build_map(dump, a.page, img_start, len(img)):
        print("  0x%05X-0x%05X  %-6s  %s" % (lo, hi, state, owner if state == "data" else ""))

    print("\nvector-table words at the usual start addresses:")
    for addr in (0x10000, 0x16000, 0x20000):
        print("  0x%05X: %s" % (addr, describe_words(words_at(dump, addr))))

    print()
    if img_start is not None:
        print("Image found, complete and unchanged, at 0x%X." % img_start)
        if img_start == expect:
            print("RESULT: it is where the devicetree expects it (0x%X)." % expect)
            print("        The flash offset is fine; look for a problem when the program starts.")
            return 0
        print("RESULT: it is NOT at 0x%X, it is at 0x%X (difference 0x%X)." % (expect, img_start, img_start - expect))
        if img_start == 2 * expect:
            print("        That is exactly double the offset: the offset was applied twice.")
        return 1
    if partial is not None:
        print("The start of the image is at 0x%X, but the data there is not identical to the file." % partial)
        for i in range(len(img)):
            if dump[partial + i] != img[i]:
                print("First difference at image offset 0x%X (flash address 0x%X)." % (i, partial + i))
                break
        return 1
    print("The image was NOT found in this dump (first 256 bytes not present anywhere).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
