#!/usr/bin/env python3
"""Export a Thing-File .po to the TSV the app loads.

The .po stays the single source of truth. Two outputs come out of it:

  * the release/package TSV (default): fuzzy entries NEVER ship -- an
    unreviewed translation rendering on a release device is a bug, not a
    feature;
  * the live-review TSV (--fuzzy): includes fuzzy entries on purpose, so a
    reviewer can drop it into $USERDATA_PATH/Thing-File/i18n/ and iterate on
    the device without a rebuild.

Entries whose printf conversions do not match their key's are refused (same
rule the runtime loader enforces), so a bad format string cannot ship or be
reviewed by accident.

    tools/i18n-po2tsv.py i18n/zh_CN.po -o build/i18n/zh_CN.tsv
    tools/i18n-po2tsv.py --fuzzy i18n/zh_CN.po -o /tmp/zh_CN-review.tsv
"""

import argparse
import re
import sys
from pathlib import Path


def c_unescape(s: str) -> str:
    return (s.replace(r"\n", "\n").replace(r"\t", "\t")
             .replace(r"\"", '"').replace("\\\\", "\\"))


def po_entries(text: str):
    """[(key, msgstr, fuzzy)] -- same parser shape as tools/i18n-extract.py."""
    entries = []
    fuzzy = False
    state = "idle"
    key, val = "", ""
    STRCONT = r'"((?:[^"\\]|\\.)*)"'

    def flush():
        nonlocal key, val, state, fuzzy
        if state == "msgstr" and key != "":
            entries.append((key, val, fuzzy))
        key, val = "", ""
        state = "idle"

    for raw in text.splitlines():
        s = raw.strip()
        if not s:
            flush()
            fuzzy = False
            continue
        if s.startswith("#"):
            if s.startswith("#,") and "fuzzy" in {flag.strip() for flag in s[2:].split(",")}:
                fuzzy = True
            continue
        if s.startswith("msgid "):
            flush()
            key = "".join(c_unescape(m) for m in re.findall(STRCONT, s[len("msgid "):]))
            state = "msgid"
            continue
        if s.startswith("msgstr "):
            val = "".join(c_unescape(m) for m in re.findall(STRCONT, s[len("msgstr "):]))
            state = "msgstr"
            continue
        m = re.fullmatch(STRCONT, s)
        if m and state == "msgid":
            key += c_unescape(m.group(1))
        elif m and state == "msgstr":
            val += c_unescape(m.group(1))
    flush()
    return entries


def fmt_sig(s: str):
    mods = []
    n = 0
    i = 0
    conv = "-+ #0123456789.*'"
    length = "hlLqjzt"
    while i < len(s):
        if s[i] != "%":
            i += 1
            continue
        i += 1
        if i < len(s) and s[i] == "%":
            i += 1
            continue
        if i >= len(s):
            return None
        while i < len(s) and s[i] in conv:
            i += 1
        mod = ""
        while i < len(s) and s[i] in length:
            mod += s[i]
            i += 1
        if i >= len(s):
            return None
        mods.append(mod + s[i])
        i += 1
        n += 1
    return n, "".join(mods)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("po")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--fuzzy", action="store_true",
                    help="include fuzzy entries (live-review table)")
    args = ap.parse_args()

    text = open(args.po, encoding="utf-8").read()
    mode = "live-review" if args.fuzzy else "release"
    lines = [f"# Generated from {args.po} by tools/i18n-po2tsv.py ({mode})."]
    if args.fuzzy:
        lines.append("# Live-override review file: includes FUZZY entries on purpose.")

    n, skipped = 0, 0
    seen = set()
    for key, val, fuzzy in po_entries(text):
        if not key or not val:
            continue
        if fuzzy and not args.fuzzy:
            continue
        if key in seen:
            print(f"skipping duplicate key: {key!r}", file=sys.stderr)
            skipped += 1
            continue
        if "\t" in key or "\n" in key or "\t" in val or "\n" in val:
            print(f"skipping (embedded tab/newline): {key!r}", file=sys.stderr)
            skipped += 1
            continue
        ka, va = fmt_sig(key), fmt_sig(val)
        if ka is None or va is None or ka != va:
            print(f"skipping (placeholder mismatch): {key!r} -> {val!r}",
                  file=sys.stderr)
            skipped += 1
            continue
        seen.add(key)
        lines.append(f"{key}\t{val}")
        n += 1

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"{args.output}: {n} entries"
          + (f", {skipped} skipped" if skipped else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
