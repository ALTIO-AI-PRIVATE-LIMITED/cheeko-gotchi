#!/usr/bin/env python3
"""Keep IPhoneAgent/Localizable.xcstrings in sync with the code.

  scripts/l10n.py sync                  add every UI string the compiler found (build first) to the catalog
  scripts/l10n.py import fr fr.json     merge {"English key": "translation"} for a language
  scripts/l10n.py check                 list keys without a translation, per language in CFBundleLocalizations

Adding a language: put its code in project.yml (CFBundleLocalizations), run `import`, run `check`.
"""
import glob
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CATALOG = ROOT / "IPhoneAgent" / "Localizable.xcstrings"
SAME = {" ", "BLE", "USB", "PHY", "PSRAM", "Wi-Fi", "{}", "ESP32 Agent", "https://your-endpoint/v1", "HID %@"}


def load():
    return json.loads(CATALOG.read_text()) if CATALOG.exists() else {"sourceLanguage": "en", "strings": {}, "version": "1.0"}


def save(cat):
    cat["strings"] = dict(sorted(cat["strings"].items()))
    CATALOG.write_text(json.dumps(cat, ensure_ascii=False, indent=2, separators=(",", " : ")) + "\n")


def languages():
    text = (ROOT / "project.yml").read_text()
    return [l for l in re.search(r"CFBundleLocalizations: \[(.*?)\]", text).group(1).replace(" ", "").split(",") if l != "en"]


def compiled_keys():
    keys = set()
    for f in glob.glob(str(ROOT / "build/Build/Intermediates.noindex/IPhoneAgent.build/*/IPhoneAgent.build/**/*.stringsdata"),
                       recursive=True):
        for entries in json.load(open(f)).get("tables", {}).values():
            keys.update(e["key"] for e in entries)
    return keys


def main(cmd, *args):
    cat = load()
    if cmd == "sync":
        keys = compiled_keys()
        if not keys:
            sys.exit("no .stringsdata found: build the app first (derivedDataPath build)")
        for k in keys:
            entry = cat["strings"].setdefault(k, {})
            if k in SAME:
                entry["shouldTranslate"] = False
        for k in set(cat["strings"]) - keys:
            cat["strings"][k]["extractionState"] = "stale"
        save(cat)
        print(f"{len(keys)} keys")
    elif cmd == "import":
        lang, path = args
        for k, v in json.loads(Path(path).read_text()).items():
            if k in cat["strings"]:
                cat["strings"][k].setdefault("localizations", {})[lang] = {"stringUnit": {"state": "translated", "value": v}}
        save(cat)
    elif cmd == "check":
        missing = 0
        for lang in languages():
            for k, e in cat["strings"].items():
                if not e.get("shouldTranslate", True) or e.get("extractionState") == "stale":
                    continue
                if lang not in e.get("localizations", {}):
                    print(f"{lang}: {k!r}")
                    missing += 1
        sys.exit(1 if missing else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(*(sys.argv[1:] or ["help"]))
