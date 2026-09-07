#!/usr/bin/env python3
"""Regression check for standard gettext fuzzy flags in both PO parsers."""

import importlib.util
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAMPLE = '#, c-format, fuzzy\nmsgid "Copy"\nmsgstr "复制"\n'


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


extract = load("i18n_extract", ROOT / "tools/i18n-extract.py")
po2tsv = load("i18n_po2tsv", ROOT / "tools/i18n-po2tsv.py")
with tempfile.TemporaryDirectory() as tmp:
    po = Path(tmp) / "test.po"
    po.write_text(SAMPLE, encoding="utf-8")
    assert extract.po_entries(po) == [("Copy", "复制", True)]
assert po2tsv.po_entries(SAMPLE) == [("Copy", "复制", True)]
print("PASS i18n-parser-test")
