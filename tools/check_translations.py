#!/usr/bin/env python3
"""Fail when an iTool PO file is incomplete or has incompatible placeholders."""

import re
import sys
from pathlib import Path

from compile_mo import read_po


PLACEHOLDER = re.compile(r"%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+|\.\*)?[hljztL]*[diuoxXfFeEgGaAcsp%]")


def placeholders(value):
    return sorted(item for item in PLACEHOLDER.findall(value) if item != "%%")


def main():
    root = Path(__file__).resolve().parents[1]
    template = read_po(root / "locale" / "iTool.pot")
    expected = {key for key in template if key}
    failed = False
    for path in sorted((root / "locale").glob("*.po")):
        catalog = read_po(path)
        missing = sorted(key for key in expected if not catalog.get(key))
        extra = sorted(key for key in catalog if key and key not in expected)
        invalid = sorted(key for key in expected if catalog.get(key) and
                         placeholders(key) != placeholders(catalog[key]))
        if missing or extra or invalid:
            failed = True
            print(f"{path.name}: missing={len(missing)}, extra={len(extra)}, placeholders={len(invalid)}")
            for label, values in (("missing", missing), ("extra", extra), ("placeholder", invalid)):
                for value in values:
                    print(f"  {label}: {value!r}")
        else:
            print(f"{path.name}: {len(expected)} translated messages OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
