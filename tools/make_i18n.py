#!/usr/bin/env python3
"""Překlady pro další jazyky (pt, es, vi) -> firmware/i18n_extra.h.

Texty v kódu jsou tr("cs", "en", "de", "it") a tabulka TOKS v portal.cpp. Pro další
jazyky se nepřepisují stovky volání: tr() najde překlad podle anglického textu
v tabulce, kterou vygeneruje tento skript z tools/i18n_extra.json
({"anglický text": ["pt", "es", "vi"], ...}). Chybějící překlad = angličtina.

  python3 tools/make_i18n.py          # vygeneruje hlavičku, vypíše chybějící
  python3 tools/make_i18n.py --dump   # vypíše všechny texty (pro překlad) jako JSON
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.join(HERE, "..", "firmware")
JSON_PATH = os.path.join(HERE, "i18n_extra.json")
LIT = r'"((?:[^"\\]|\\.)*)"'


def unc(s):  # C literál -> text
    return bytes(s, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def cq(s):  # text -> C literál
    out = []
    for ch in s:
        if ch == "\\": out.append("\\\\")
        elif ch == '"': out.append('\\"')
        elif ch == "\n": out.append("\\n")
        else: out.append(ch)
    return '"' + "".join(out) + '"'


def parse_args(src, i):
    """Od pozice za 'tr(' přečte argumenty složené z řetězcových literálů."""
    args, cur = [], []
    while i < len(src):
        c = src[i]
        if c.isspace(): i += 1; continue
        if src.startswith("//", i): i = src.index("\n", i); continue
        if c == '"':
            m = re.compile(LIT).match(src, i)
            cur.append(m.group(1)); i = m.end(); continue
        if c in ",)":
            if not cur: return None, i
            args.append(unc("".join(cur))); cur = []
            i += 1
            if c == ")": return args, i
            continue
        return None, i
    return None, i


def collect():
    items = {}  # en -> (cs, de, it, kde)
    bad = []
    for fn in sorted(os.listdir(FW)):
        if not fn.endswith((".cpp", ".h")) or fn in ("i18n.h", "i18n_extra.h"): continue
        src = open(os.path.join(FW, fn), encoding="utf-8").read()
        # tr(...) a makro W(...) v forecast.cpp; tr(h.n[0], ...) z tabulky svátků se přeskočí
        for m in re.finditer(r"(?<![\w.])(?:tr|W)\(", src):
            if src.startswith("tr(h.n[0]", m.start()) or src.startswith("W(cs, en", m.start()) or \
               src.startswith("tr(cs, en", m.start()):
                continue
            args, _ = parse_args(src, m.end())
            line = src.count("\n", 0, m.start()) + 1
            if src[m.start()] == "W" and not args: continue
            if not args or len(args) != 4:
                bad.append(f"{fn}:{line}"); continue
            items.setdefault(args[1], (args[0], args[2], args[3], f"{fn}:{line}"))
        if fn == "main.cpp":  # tabulka státních svátků {měsíc, den, {"cs", "en", "de", "it"}}
            for m in re.finditer(r"\{\d+, \d+, \{" + r",\s*".join([LIT] * 4) + r"\}\}", src):
                cs, en, de, it = [unc(x) for x in m.groups()]
                items.setdefault(en, (cs, de, it, "svátek"))
        if fn == "portal.cpp":
            for m in re.finditer(r'\{"(\w+)", \{' + r",\s*".join([LIT] * 4) + r"\}\}", src):
                k, cs, en, de, it = m.group(1), *[unc(x) for x in m.groups()[1:]]
                if k in ("lang", "locale"): continue  # řeší se zvlášť
                items.setdefault(en, (cs, de, it, f"TOKS {k}"))
    return items, bad


def main():
    items, bad = collect()
    if bad:
        print("Nečitelná volání tr():", ", ".join(bad)); sys.exit(1)
    if "--dump" in sys.argv:
        json.dump({en: {"cs": v[0], "de": v[1], "it": v[2], "at": v[3]} for en, v in items.items()},
                  sys.stdout, ensure_ascii=False, indent=1); return
    tr = json.load(open(JSON_PATH, encoding="utf-8")) if os.path.exists(JSON_PATH) else {}
    missing = [en for en in items if en not in tr]
    unused = [en for en in tr if en not in items]
    rows = sorted((en.encode("utf-8"), en, tr[en]) for en in items if en in tr)
    with open(os.path.join(FW, "i18n_extra.h"), "w", encoding="utf-8") as f:
        f.write("#pragma once\n// Vygenerováno tools/make_i18n.py z tools/i18n_extra.json – needitovat ručně.\n")
        f.write("// Seřazeno podle anglického textu (bajtově) pro binární vyhledávání.\n\n")
        f.write("struct TrExtra {\n  const char* en;\n  const char* t[3];  // pt, es, vi\n};\n\n")
        f.write(f"static const TrExtra TR_EXTRA[{len(rows)}] = {{\n")
        fmt = lambda x: re.findall(r"%[-+#0]*\d*(?:\.\d+)?[a-z%]", x)
        for _, en, t in rows:
            if len(t) != 3: sys.exit(f"Překlad musí mít 3 jazyky: {en}")
            for x in t:  # stejné formátovací značky ve stejném pořadí (jinak by printf spadl)
                if fmt(x) != fmt(en): sys.exit(f"Nesedí %-značky: {en!r} -> {x!r}")
                if "#" in en and "#" not in x: sys.exit(f"Chybí #: {en!r} -> {x!r}")
            f.write(f"    {{{cq(en)}, {{{cq(t[0])}, {cq(t[1])}, {cq(t[2])}}}}},\n")
        f.write("};\n")
    print(f"textů {len(items)}, přeloženo {len(rows)}, chybí {len(missing)}, nepoužité {len(unused)}")
    for en in missing: print("  CHYBÍ:", en)
    for en in unused: print("  NEPOUŽITÉ:", en)
    if "--strict" in sys.argv and missing: sys.exit(1)


main()
