#!/usr/bin/env python3
"""Vyřízne z efontJA (LovyanGFX) jen latinku + češtinu -> firmware/fonts_cz.h
a doplní složená vietnamská písmena (tools/viet_glyphs.py).

Použití: python3 tools/make_fonts.py <cesta k LovyanGFX>/src/lgfx/Fonts/efont/lgfx_efont_ja.c
Plný efontJA má ~10 000 znaků (stovky kB na velikost); podmnožina má pár kB,
díky čemuž se firmware vejde do OTA oddílů.
"""
import re, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from u8g2font import Font
from viet_glyphs import vi_chars, compose

SIZES = ["10", "12", "16", "24"]
# tučné / kurzíva / tučná kurzíva (pro obrazovku Poznámka)
STYLED = [sz + st for sz in ["12", "16", "24"] for st in ["_b", "_i", "_bi"]]
EXTRA = list(range(0x100, 0x180)) + [0x2013, 0x2014, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193]

def load(src):
    fonts = {}
    pat = r'const uint8_t (lgfx_efont_ja_(\d+(?:_b|_i|_bi)?))\[\d*\][^=]*=\s*"((?:[^"\\]|\\.|"\s*")*)"'
    for m in re.finditer(pat, src):
        s = re.sub(r'"\s*"', '', m.group(3))
        fonts[m.group(2)] = s.encode('latin1').decode('unicode_escape').encode('latin1')
    return fonts

def subset(b):
    up = (b[21] << 8) | b[22]
    base = 23 + up
    p = base
    while True:  # přeskoč jump tabulku
        enc = (b[p + 2] << 8) | b[p + 3]
        p += 4
        if enc == 0xFFFF:
            break
    glyphs = {}
    while p + 3 <= len(b):
        enc = (b[p] << 8) | b[p + 1]
        if enc == 0:
            break
        sz = b[p + 2]
        glyphs[enc] = b[p:p + sz]
        p += sz
    out = bytearray(b[:base])
    out += bytes([0, 4, 0xFF, 0xFF])  # jediný záznam jump tabulky
    for e in sorted(set(EXTRA) & glyphs.keys()):
        out += glyphs[e]
    out += b'\x00\x00'
    return bytes(out)

def add_vietnamese(b, size):
    """Doplní do podmnožiny složená vietnamská písmena (seřazeně mezi ostatní)."""
    f = Font(b)
    up = (b[21] << 8) | b[22]
    base = 23 + up + 4  # za jedinou položkou jump tabulky
    raw, p = {}, base
    while True:
        enc = (b[p] << 8) | b[p + 1]
        if enc == 0: break
        raw[enc] = b[p:p + b[p + 2]]; p += b[p + 2]
    added = 0
    for cp in vi_chars():
        if cp in raw or cp < 0x100: continue
        g = compose(f, cp, size)
        if not g: raise SystemExit(f"nelze složit U+{cp:04X}")
        bits = f.encode(g)
        raw[cp] = bytes([cp >> 8, cp & 0xFF, 3 + len(bits)]) + bits
        added += 1
    out = bytearray(b[:base])
    for e in sorted(raw): out += raw[e]
    out += b'\x00\x00'
    return bytes(out), added

def main():
    src = open(sys.argv[1], encoding='latin1').read()
    fonts = load(src)
    here = os.path.dirname(os.path.abspath(__file__))
    dst = os.path.join(here, '..', 'firmware', 'fonts_cz.h')
    with open(dst, 'w') as f:
        f.write('#pragma once\n// Vygenerováno tools/make_fonts.py z efontJA (LovyanGFX), licence viz efont COPYRIGHT.\n')
        f.write('#include <LovyanGFX.hpp>\n\n')
        for s in SIZES + STYLED:
            d, nvi = add_vietnamese(subset(fonts[s]), int(s.split('_')[0]))
            f.write(f'static const uint8_t font_cz_{s}_data[{len(d)}] = {{\n')
            for i in range(0, len(d), 24):
                f.write('  ' + ','.join(str(x) for x in d[i:i + 24]) + ',\n')
            f.write('};\n')
            f.write(f'static const lgfx::U8g2font fontCZ_{s} = {{ font_cz_{s}_data }};\n\n')
            print(f'efontJA_{s}: {len(fonts[s])} B -> {len(d)} B (+{nvi} vietnamských)')

main()
