#!/usr/bin/env python3
"""Generate complete gettext catalogs for additional iTool languages."""

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
LOCALE = ROOT / "locale"
CACHE = ROOT / "build" / "language-translation-cache.json"
LANGUAGES = {
    "bg": "bg", "ja": "ja", "cs": "cs", "es": "es", "et": "et",
    "hr": "hr", "it": "it", "pl": "pl", "pt_BR": "pt-BR", "ru": "ru",
    "sl": "sl", "tr": "tr", "vi": "vi", "ko": "ko",
}
PLACEHOLDER = re.compile(r"%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.\d+|\.\*)?[hljztL]*[diuoxXfFeEgGaAcsp%]")
SEPARATOR_TOKEN = "9876543210123456789"
SEPARATOR = "\n" + SEPARATOR_TOKEN + "\n"


def protect(text):
    values = []
    def replace(match):
        values.append(match.group(0))
        return "ZXQPLACEHOLDER%dQXZ" % (len(values) - 1)
    return PLACEHOLDER.sub(replace, text), values


def restore(text, values):
    for index, value in enumerate(values):
        pattern = re.compile(r"ZXQ\s*PLACEHOLDER\s*%d\s*QXZ" % index, re.I)
        text = pattern.sub(lambda _: value, text)
    return text


def request_translation(text, target):
    query = urllib.parse.urlencode({
        "client": "dict-chrome-ex", "sl": "en", "tl": target, "q": text,
    })
    request = urllib.request.Request(
        "https://clients5.google.com/translate_a/t?" + query,
        headers={"User-Agent": "Mozilla/5.0 iTool-localization"})
    for attempt in range(6):
        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                data = json.loads(response.read().decode("utf-8"))
            return "".join(part[0] for part in data[0] if part[0])
        except Exception:
            if attempt == 5:
                raise
            time.sleep(5 * (attempt + 1))


def translate_batch(messages, target):
    protected = []
    placeholders = []
    for message in messages:
        text, values = protect(message)
        protected.append(text)
        placeholders.append(values)
    translated = request_translation(SEPARATOR.join(protected), target)
    parts = re.split(r"\s*" + SEPARATOR_TOKEN + r"\s*", translated)
    if len(parts) != len(messages):
        parts = [request_translation(text, target) for text in protected]
    return [restore(text.strip(), values) for text, values in zip(parts, placeholders)]


def quote(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"').replace("\r", "\\r").replace("\n", "\\n").replace("\t", "\\t") + '"'


def write_catalog(language, entries, messages):
    lines = ['msgid ""', 'msgstr ""',
             '"Project-Id-Version: iTool\\n"', '"Language: %s\\n"' % language,
             '"MIME-Version: 1.0\\n"', '"Content-Type: text/plain; charset=UTF-8\\n"',
             '"Content-Transfer-Encoding: 8bit\\n"']
    for message in messages:
        lines.extend(["", "msgid " + quote(message), "msgstr " + quote(entries[message])])
    (LOCALE / (language + ".po")).write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def generate(language, target, messages, cache):
    entries = cache.setdefault(language, {})
    missing = [message for message in messages if not entries.get(message)]
    for offset in range(0, len(missing), 24):
        batch = missing[offset:offset + 24]
        translated = translate_batch(batch, target)
        entries.update(zip(batch, translated))
        if offset % 120 == 0:
            print("%s: %d/%d" % (language, min(offset + len(batch), len(missing)), len(missing)), flush=True)
    return language, entries


def main():
    if "--apply" not in sys.argv:
        raise SystemExit("Pass --apply to generate catalogs")
    messages = sorted(key for key in read_po(LOCALE / "en.po") if key)
    cache = json.loads(CACHE.read_text(encoding="utf-8")) if CACHE.exists() else {}
    results = {}
    with ThreadPoolExecutor(max_workers=7) as pool:
        futures = {pool.submit(generate, language, target, messages, cache): language
                   for language, target in LANGUAGES.items()}
        for future in as_completed(futures):
            language = futures[future]
            try:
                language, entries = future.result()
                results[language] = entries
            finally:
                # Preserve completed batches from every worker for resumable runs.
                CACHE.parent.mkdir(parents=True, exist_ok=True)
                CACHE.write_text(json.dumps(cache, ensure_ascii=False, indent=2), encoding="utf-8")
            
            CACHE.parent.mkdir(parents=True, exist_ok=True)
            CACHE.write_text(json.dumps(cache, ensure_ascii=False, indent=2), encoding="utf-8")
            print(language + ": complete", flush=True)
    for language in LANGUAGES:
        write_catalog(language, results[language], messages)


if __name__ == "__main__":
    main()
