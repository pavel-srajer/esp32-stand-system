#!/usr/bin/env python3
"""Zkontroluje syntaxi JavaScriptu webových stránek (portal.cpp) ve všech jazycích.

Překlady {{klíč}} se vkládají i do skriptů; chybný znak v překladu (např. apostrof)
by rozbil celý skript a s ním mapu i ovládání. Potřebuje Node.js (`node --check`).
"""
import re, subprocess, sys, tempfile, os

src = open(os.path.join(os.path.dirname(__file__), "..", "firmware", "portal.cpp"), encoding="utf-8").read()

def cstr(x):
    return bytes(x, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")

toks = {}
for k, v in re.findall(r'\{"(\w+)", \{((?:"(?:[^"\\]|\\.)*",?\s*){4})\}\}', src):
    toks[k] = [cstr(x) for x in re.findall(r'"((?:[^"\\]|\\.)*)"', v)]

# pt/es/vi: překlad podle angličtiny z tools/i18n_extra.json (jako trExtra() ve firmwaru)
import json
extra = json.load(open(os.path.join(os.path.dirname(__file__), "i18n_extra.json"), encoding="utf-8"))
CODES = {"lang": ["pt", "es", "vi"], "locale": ["pt-PT", "es-ES", "vi-VN"]}
for k, v in toks.items():
    v += CODES[k] if k in CODES else [extra.get(v[1], [v[1]] * 3)[i] for i in range(3)]

pages = dict(re.findall(r'static const char (PAGE_\w+)\[\] PROGMEM = R"HTML\((.*?)\)HTML"', src, re.S))
bad = 0
with tempfile.TemporaryDirectory() as d:
    for L, lang in enumerate(["cs", "en", "de", "it", "pt", "es", "vi"]):
        for name, tpl in pages.items():
            # stejně jako localize() ve firmwaru: ' -> ’
            out = re.sub(r"\{\{(\w+)\}\}", lambda m: toks[m.group(1)][L].replace("'", "’") if m.group(1) in toks else m.group(0), tpl)
            for i, sc in enumerate(re.findall(r"<script>(.*?)(?:</script>|$)", out, re.S)):
                f = os.path.join(d, f"{lang}_{name}_{i}.js")
                open(f, "w", encoding="utf-8").write("var ST=[],RK=1;" + sc)
                r = subprocess.run(["node", "--check", f], capture_output=True, text=True)
                if r.returncode:
                    bad += 1
                    print(f"CHYBA {lang} {name}: {r.stderr.strip().splitlines()[-1]}")
print("JS stránek: chyb", bad)
sys.exit(1 if bad else 0)
