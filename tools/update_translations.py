#!/usr/bin/env python3
"""Extract ITOOL_TR literals and update iTool gettext source catalogs."""

import ast
import re
from pathlib import Path

from compile_mo import read_po


ROOT = Path(__file__).resolve().parents[1]
LOCALE = ROOT / "locale"
MESSAGE = re.compile(r'ITOOL_TR\("((?:[^"\\]|\\.)*)"\)')


def messages():
    found = set()
    for path in (ROOT / "src").rglob("*.cpp"):
        source = path.read_text(encoding="utf-8")
        found.update(ast.literal_eval('"' + item + '"') for item in MESSAGE.findall(source))
    return sorted(found)


def quoted(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def header(language):
    return {
        "": "Project-Id-Version: iTool\nLanguage: %s\nMIME-Version: 1.0\n"
            "Content-Type: text/plain; charset=UTF-8\nContent-Transfer-Encoding: 8bit\n" % language
    }


def write(path, language, entries, template=False):
    lines = ["msgid \"\"", "msgstr \"\"",
             '"Project-Id-Version: iTool\\n"',
             '"Language: %s\\n"' % language,
             '"MIME-Version: 1.0\\n"',
             '"Content-Type: text/plain; charset=UTF-8\\n"',
             '"Content-Transfer-Encoding: 8bit\\n"']
    for message in messages():
        lines.extend(["", "msgid " + quoted(message),
                      "msgstr " + quoted("" if template else entries.get(message, ""))])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    LOCALE.mkdir(exist_ok=True)
    write(LOCALE / "iTool.pot", "", {}, template=True)
    write(LOCALE / "en.po", "en", {message: message for message in messages()})
    for path in sorted(LOCALE.glob("*.po")):
        language = path.stem
        if language == "en":
            continue
        entries = read_po(path) if path.exists() else header(language)
        write(path, language, entries)


if __name__ == "__main__":
    main()
