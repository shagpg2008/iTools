#!/usr/bin/env python3
"""One-time migration of Chinese wxS literals into gettext catalogs."""

import ast
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from compile_mo import read_po


ROOT = Path(__file__).resolve().parents[1]
CACHE = ROOT / "build" / "ui-translation-cache.json"
LITERAL = re.compile(r'wxS\("((?:[^"\\]|\\.)*[\u3400-\u9fff](?:[^"\\]|\\.)*)"\)')
PLACEHOLDER = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+|\.\*)?[hljztL]*[diuoxXfFeEgGaAcsp%]')


def c_decode(value):
    return ast.literal_eval('"' + value + '"')


def c_encode(value):
    return value.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '\\r').replace('\n', '\\n').replace('\t', '\\t')


def protect(value):
    placeholders = []
    def replace(match):
        placeholders.append(match.group(0))
        return f"ZXQPH{len(placeholders) - 1}QXZ"
    return PLACEHOLDER.sub(replace, value), placeholders


def restore(value, placeholders):
    for index, placeholder in enumerate(placeholders):
        token = f"ZXQPH{index}QXZ"
        variants = (token, token.lower(), token.replace("PH", " PH "))
        for variant in variants:
            value = value.replace(variant, placeholder)
    return value


def translate_one(text, language):
    protected, placeholders = protect(text)
    query = urllib.parse.urlencode({
        "client": "gtx", "sl": "zh-CN", "tl": language, "dt": "t", "q": protected
    })
    request = urllib.request.Request(
        "https://translate.googleapis.com/translate_a/single?" + query,
        headers={"User-Agent": "Mozilla/5.0 iTool-localization"})
    for attempt in range(5):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                data = json.loads(response.read().decode("utf-8"))
            translated = "".join(part[0] for part in data[0] if part[0])
            return restore(translated, placeholders)
        except Exception:
            if attempt == 4:
                raise
            time.sleep(1.5 * (attempt + 1))


def load_cache():
    if CACHE.exists():
        return json.loads(CACHE.read_text(encoding="utf-8"))
    return {"en": {}, "zh-TW": {}}


def save_cache(cache):
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(json.dumps(cache, ensure_ascii=False, indent=2), encoding="utf-8")


def collect():
    files = []
    messages = set()
    for path in (ROOT / "src").rglob("*"):
        if path.suffix not in (".cpp", ".h") or path.name == "Localization.cpp":
            continue
        source = path.read_text(encoding="utf-8")
        found = {c_decode(value) for value in LITERAL.findall(source)}
        if found:
            files.append(path)
            messages.update(found)
    return files, sorted(messages)


def translate_missing(cache, messages, language):
    missing = [message for message in messages if message not in cache[language]]
    print(f"Translating {len(missing)} messages to {language}...", flush=True)
    with ThreadPoolExecutor(max_workers=8) as pool:
        futures = {pool.submit(translate_one, message, language): message for message in missing}
        for number, future in enumerate(as_completed(futures), 1):
            message = futures[future]
            cache[language][message] = future.result()
            if number % 25 == 0:
                save_cache(cache)
                print(f"  {number}/{len(missing)}", flush=True)
    save_cache(cache)


def ensure_include(source):
    include = '#include "core/Localization.h"\n'
    if include.strip() in source:
        return source
    first = source.find("\n")
    return source[:first + 1] + "\n" + include + source[first + 1:]


def po_quote(value):
    return '"' + c_encode(value) + '"'


def write_catalog(path, language, entries):
    lines = ["msgid \"\"", "msgstr \"\"",
             '"Project-Id-Version: iTool\\n"', f'"Language: {language}\\n"',
             '"MIME-Version: 1.0\\n"', '"Content-Type: text/plain; charset=UTF-8\\n"',
             '"Content-Transfer-Encoding: 8bit\\n"']
    for message in sorted(entries):
        lines.extend(["", "msgid " + po_quote(message), "msgstr " + po_quote(entries[message])])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def main():
    if "--apply" not in sys.argv:
        raise SystemExit("Pass --apply to modify sources and catalogs")
    files, messages = collect()
    print(f"Found {len(messages)} unique messages in {len(files)} files")
    cache = load_cache()
    translate_missing(cache, messages, "en")
    translate_missing(cache, messages, "zh-TW")

    old_catalogs = {code: read_po(ROOT / "locale" / f"{code}.po")
                    for code in ("en", "zh_CN", "zh_TW")}
    migrated = {}
    for chinese in messages:
        english = cache["en"][chinese]
        migrated.setdefault(english, (chinese, cache["zh-TW"][chinese]))

    for path in files:
        source = path.read_text(encoding="utf-8")
        def replace(match):
            chinese = c_decode(match.group(1))
            return 'ITOOL_TR("' + c_encode(cache["en"][chinese]) + '")'
        updated = LITERAL.sub(replace, source)
        path.write_text(ensure_include(updated), encoding="utf-8", newline="\n")

    entries = {code: {key: value for key, value in catalog.items() if key}
               for code, catalog in old_catalogs.items()}
    for english, (chinese, traditional) in migrated.items():
        entries["en"][english] = english
        entries["zh_CN"][english] = chinese
        entries["zh_TW"][english] = traditional
    all_keys = set(entries["en"])
    write_catalog(ROOT / "locale" / "en.po", "en", {key: entries["en"][key] for key in all_keys})
    write_catalog(ROOT / "locale" / "zh_CN.po", "zh_CN", {key: entries["zh_CN"].get(key, key) for key in all_keys})
    write_catalog(ROOT / "locale" / "zh_TW.po", "zh_TW", {key: entries["zh_TW"].get(key, key) for key in all_keys})
    write_catalog(ROOT / "locale" / "iTool.pot", "", {key: "" for key in all_keys})
    print(f"Migrated {len(messages)} messages; catalogs now contain {len(all_keys)} entries")


if __name__ == "__main__":
    main()
