#!/usr/bin/env python3
"""Vygeneruje firmware/airlines.h – názvy leteckých společností podle ICAO kódu.

Zdroj: Virtual Radar Server standing data (CC0), airlines/schema-01/airlines.csv
       https://github.com/vradarserver/standing-data

Použití: python3 tools/make_airlines.py <cesta>/airlines.csv
"""
import csv
import os
import sys
import unicodedata

# znaky, které umí písmo na displeji: ASCII, Latin-1, Latin Extended-A
def displayable(ch):
    o = ord(ch)
    return o < 0x7F or 0xA0 <= o <= 0x17F


def clean(name):
    name = ' '.join(name.split())
    if all(displayable(c) for c in name):
        return name
    # jiné písmo -> latinka bez diakritiky, co nejde, vynechat
    out = ''.join(c if displayable(c) else unicodedata.normalize('NFKD', c).encode('ascii', 'ignore').decode()
                  for c in name)
    return ' '.join(out.split())


def main():
    rows = list(csv.DictReader(open(sys.argv[1], encoding='utf-8-sig')))
    names = {}
    for r in rows:
        icao, name = (r.get('ICAO') or '').strip(), clean(r.get('Name') or '')
        if len(icao) == 3 and icao.isalpha() and icao.isupper() and name and icao not in names:
            names[icao] = name
    codes = sorted(names)
    blob, offs = bytearray(), []
    for c in codes:
        offs.append(len(blob))
        blob += names[c].encode('utf-8') + b'\0'

    def cstr(b):
        return ''.join(f'\\{x:03o}' if x < 32 or x >= 127 or x in (34, 92) or chr(x) == '?' else chr(x) for x in b)

    out = ['#pragma once',
           '// Vygenerováno tools/make_airlines.py – názvy leteckých společností podle ICAO kódu.',
           '// Zdroj: Virtual Radar Server standing data (CC0), github.com/vradarserver/standing-data',
           '#include <stdint.h>', '',
           f'#define AIRLINE_COUNT {len(codes)}',
           '// seřazené ICAO kódy po 3 znacích (binární vyhledávání)',
           'static const char AIRLINE_CODES[] =']
    s = ''.join(codes)
    for i in range(0, len(s), 120):
        out.append(f'    "{s[i:i + 120]}"')
    out[-1] += ';'
    out.append(f'static const uint32_t AIRLINE_OFF[{len(offs)}] = {{')
    for i in range(0, len(offs), 16):
        out.append('  ' + ','.join(map(str, offs[i:i + 16])) + ',')
    out += ['};', 'static const char AIRLINE_NAMES[] =']
    for i in range(0, len(blob), 96):
        out.append(f'    "{cstr(blob[i:i + 96])}"')
    out[-1] += ';'
    out.append('')
    dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'firmware', 'airlines.h')
    open(dst, 'w', encoding='utf-8').write('\n'.join(out))
    print(f'{len(codes)} společností, {len(s) + len(offs) * 4 + len(blob)} B')


main()
