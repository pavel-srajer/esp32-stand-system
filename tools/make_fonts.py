#!/usr/bin/env python3
"""Vyřízne z efontJA (LovyanGFX) jen latinku + češtinu -> firmware/fonts_cz.h.

Použití: python3 tools/make_fonts.py <cesta k LovyanGFX>/src/lgfx/Fonts/efont/lgfx_efont_ja.c
Plný efontJA má ~10 000 znaků (stovky kB na velikost); podmnožina má pár kB,
díky čemuž se firmware vejde do OTA oddílů.
"""
import re, sys, os

SIZES = ["10", "12", "16", "24"]
EXTRA = list(range(0x100, 0x180)) + [0x2013, 0x2014, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192, 0x2193]

def load(src):
    fonts = {}
    pat = r'const uint8_t (lgfx_efont_ja_(\d+))\[\d*\][^=]*=\s*"((?:[^"\\]|\\.|"\s*")*)"'
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

def main():
    src = open(sys.argv[1], encoding='latin1').read()
    fonts = load(src)
    here = os.path.dirname(os.path.abspath(__file__))
    dst = os.path.join(here, '..', 'firmware', 'fonts_cz.h')
    with open(dst, 'w') as f:
        f.write('#pragma once\n// Vygenerováno tools/make_fonts.py z efontJA (LovyanGFX), licence viz efont COPYRIGHT.\n')
        f.write('#include <LovyanGFX.hpp>\n\n')
        for s in SIZES:
            d = subset(fonts[s])
            f.write(f'static const uint8_t font_cz_{s}_data[{len(d)}] = {{\n')
            for i in range(0, len(d), 24):
                f.write('  ' + ','.join(str(x) for x in d[i:i + 24]) + ',\n')
            f.write('};\n')
            f.write(f'static const lgfx::U8g2font fontCZ_{s} = {{ font_cz_{s}_data }};\n\n')
            print(f'efontJA_{s}: {len(fonts[s])} B -> {len(d)} B')

main()
