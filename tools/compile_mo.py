#!/usr/bin/env python3
"""Compile the small iTool gettext PO catalogs without external dependencies."""

import ast
import struct
import sys
from pathlib import Path


def read_po(path):
    messages = {}
    current_id = None
    current_value = None
    section = None

    def commit():
        if current_id is not None and current_value is not None:
            messages[current_id] = current_value

    for raw in path.read_text(encoding="utf-8").splitlines() + ["msgid \"__EOF__\""]:
        line = raw.strip()
        if line.startswith("msgid "):
            commit()
            current_id = ast.literal_eval(line[6:])
            current_value = None
            section = "id"
        elif line.startswith("msgstr "):
            current_value = ast.literal_eval(line[7:])
            section = "value"
        elif line.startswith('"'):
            value = ast.literal_eval(line)
            if section == "id":
                current_id += value
            elif section == "value":
                current_value += value
    messages.pop("__EOF__", None)
    return messages


def write_mo(messages, path):
    keys = sorted(messages)
    ids = [key.encode("utf-8") for key in keys]
    values = [messages[key].encode("utf-8") for key in keys]
    count = len(keys)
    original_table = 28
    translation_table = original_table + count * 8
    data_offset = translation_table + count * 8
    id_data = b"\0".join(ids) + b"\0"
    value_offset = data_offset + len(id_data)

    header = struct.pack("<7I", 0x950412DE, 0, count, original_table,
                         translation_table, 0, 0)
    originals = b"".join(struct.pack("<2I", len(value), data_offset + offset)
                         for offset, value in offsets(ids))
    translations = b"".join(struct.pack("<2I", len(value), value_offset + offset)
                            for offset, value in offsets(values))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + originals + translations + id_data +
                     b"\0".join(values) + b"\0")


def offsets(values):
    offset = 0
    for value in values:
        yield offset, value
        offset += len(value) + 1


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: compile_mo.py INPUT.po OUTPUT.mo")
    write_mo(read_po(Path(sys.argv[1])), Path(sys.argv[2]))


if __name__ == "__main__":
    main()
