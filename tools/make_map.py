#!/usr/bin/env python3
"""Vygeneruje firmware/mapdata.h – obrysová mapa pro pozadí meteoradaru.

Zdroje:
  * Natural Earth 10m (volné dílo): hranice států, řeky, velká města sousedních zemí
    https://github.com/nvkelso/natural-earth-vector  (složka geojson/)
  * Česká města (souřadnice, počet obyvatel): janpipek/cli-plotting-talk cities.csv

Použití:
  python3 tools/make_map.py <natural-earth-vector/geojson> <cities.csv>
"""
import csv
import json
import math
import os
import sys

LON0, LAT0, Q = 15.0, 49.8, 0.0003          # kvantizace: 0,0003° ≈ 20–33 m
BBOX = (9.0, 47.0, 21.6, 52.7)               # ČR + okolí ~200 km
TOL = 0.004                                  # zjednodušení čar ≈ 300–450 m


# česká exonyma pro města v okolí
EXONYM = {
    'Berlin': 'Berlín', 'Vienna': 'Vídeň', 'Warsaw': 'Varšava', 'Munich': 'Mnichov', 'Dresden': 'Drážďany',
    'Leipzig': 'Lipsko', 'Nürnberg': 'Norimberk', 'Nuremberg': 'Norimberk', 'Regensburg': 'Řezno', 'Passau': 'Pasov',
    'Wrocław': 'Vratislav', 'Katowice': 'Katovice', 'Kraków': 'Krakov', 'Linz': 'Linec', 'Graz': 'Štýrský Hradec',
    'Hanover': 'Hannover', 'Görlitz': 'Zhořelec', 'Zittau': 'Žitava', 'Bamberg': 'Bamberk', 'Würzburg': 'Würzburg',
    'Budapest': 'Budapešť', 'Košice': 'Košice', 'Žilina': 'Žilina', 'Opole': 'Opolí', 'Gliwice': 'Gliwice',
    'Salzburg': 'Salcburk', 'Bayreuth': 'Bayreuth', 'Chemnitz': 'Chemnitz', 'Augsburg': 'Augsburk',
    'Cottbus': 'Chotěbuz', 'Bautzen': 'Budyšín', 'Wałbrzych': 'Valbřich', 'Legnica': 'Lehnice', 'Racibórz': 'Ratiboř',
    'Bielsko-Biała': 'Bílsko-Bělá', 'Częstochowa': 'Čenstochová', 'Łódź': 'Lodž', 'Poznań': 'Poznaň',
    'Sankt Pölten': 'Sankt Pölten', 'Wiener Neustadt': 'Vídeňské Nové Město', 'Bratislava': 'Bratislava',
    'Trnava': 'Trnava', 'Nitra': 'Nitra', 'Trenčín': 'Trenčín', 'Erfurt': 'Erfurt', 'Halle': 'Halle',
    'Magdeburg': 'Magdeburk', 'Frankfurt': 'Frankfurt', 'Zwickau': 'Cvikov', 'Plauen': 'Plavno', 'Hof': 'Hof',
}


def short(name):
    """Zkrátí dlouhé názvy: Ústí nad Labem -> Ústí n. L., Brandýs nad Labem-Stará Boleslav -> Brandýs n. L."""
    if len(name) <= 14:
        return name
    if '-' in name:
        name = name.split('-')[0]
    w = name.split(' ')
    for i, x in enumerate(w[:-1]):
        if x in ('nad', 'pod', 'na', 'u') and i + 1 < len(w):
            return ' '.join(w[:i]) + ' ' + x[0] + '. ' + w[i + 1][0] + '.'
    return name


def inside(lon, lat):
    return BBOX[0] <= lon <= BBOX[2] and BBOX[1] <= lat <= BBOX[3]


def dp(pts, tol):
    """Douglas–Peucker."""
    if len(pts) < 3:
        return pts
    (x1, y1), (x2, y2) = pts[0], pts[-1]
    dx, dy = x2 - x1, y2 - y1
    L = math.hypot(dx, dy) or 1e-12
    best, idx = -1, 0
    for i in range(1, len(pts) - 1):
        x, y = pts[i]
        d = abs(dy * x - dx * y + x2 * y1 - y2 * x1) / L
        if d > best:
            best, idx = d, i
    if best <= tol:
        return [pts[0], pts[-1]]
    return dp(pts[:idx + 1], tol)[:-1] + dp(pts[idx:], tol)


def lines_of(geom):
    t, c = geom['type'], geom['coordinates']
    if t == 'LineString':
        return [c]
    if t == 'MultiLineString':
        return c
    return []


def clip_runs(line):
    """Rozdělí čáru na úseky uvnitř BBOX (s jedním bodem přesahu kvůli plynulosti)."""
    runs, cur = [], []
    for i, (lon, lat) in enumerate(line):
        if inside(lon, lat):
            if not cur and i > 0:
                cur.append(tuple(line[i - 1][:2]))
            cur.append((lon, lat))
        elif cur:
            cur.append((lon, lat))
            runs.append(cur)
            cur = []
    if cur:
        runs.append(cur)
    return [r for r in runs if len(r) >= 2]


def main():
    ne, cities_csv = sys.argv[1], sys.argv[2]
    polylines = []  # (kind, [(lon,lat)])

    def add(path, kind, keep=lambda p: True):
        for f in json.load(open(os.path.join(ne, path)))['features']:
            if not keep(f['properties']):
                continue
            for line in lines_of(f['geometry']):
                for run in clip_runs(line):
                    s = dp(run, TOL)
                    if len(s) >= 2:
                        polylines.append((kind, s))

    add('ne_10m_admin_0_boundary_lines_land.geojson', 0)
    add('ne_10m_rivers_lake_centerlines.geojson', 1)
    add('ne_10m_rivers_europe.geojson', 2)  # menší řeky – jen při menším poloměru

    pts, lines = [], []
    for kind, s in polylines:
        q = [(round((lon - LON0) / Q), round((lat - LAT0) / Q)) for lon, lat in s]
        dedup = [q[0]] + [p for a, p in zip(q, q[1:]) if p != a]
        if len(dedup) < 2:
            continue
        lines.append((len(pts), len(dedup), kind))
        pts.extend(dedup)

    # města: ČR z cities.csv, okolní státy z Natural Earth
    cities = []
    for r in csv.DictReader(open(cities_csv, encoding='utf-8')):
        cities.append((short(r['city']), float(r['latitude']), float(r['longitude']), int(float(r['population']))))
    for f in json.load(open(os.path.join(ne, 'ne_10m_populated_places_simple.geojson')))['features']:
        p = f['properties']
        lat, lon = p['latitude'], p['longitude']
        if p.get('adm0name') == 'Czechia' or p.get('sov0name') in ('Czechia', 'Czech Republic') or not inside(lon, lat):
            continue
        name = EXONYM.get(p['name'], p['name'])
        pop = int(p.get('pop_max') or 0)
        if pop < 50000:
            continue
        if any(math.hypot((lon - c[2]) * 0.65, lat - c[1]) < 0.05 for c in cities):
            continue
        cities.append((short(name), lat, lon, pop))
    cities.sort(key=lambda c: -c[3])  # od největších (kvůli překrývání popisků)

    out = ['#pragma once',
           '// Vygenerováno tools/make_map.py – obrysová mapa pro pozadí meteoradaru.',
           '// Hranice a řeky: Natural Earth 10m (public domain). Česká města: janpipek/cli-plotting-talk.',
           '#include <stdint.h>', '',
           f'#define MAP_LON0 {LON0}f', f'#define MAP_LAT0 {LAT0}f', f'#define MAP_Q {Q}f', '',
           'struct MapLine { uint16_t start, n; uint8_t kind; };  // kind: 0 hranice, 1 velká řeka, 2 menší řeka',
           'struct MapCity { const char* name; int16_t x, y; uint32_t pop; };', '',
           f'static const int16_t MAP_PTS[{len(pts)}][2] = {{']
    for i in range(0, len(pts), 8):
        out.append('  ' + ', '.join(f'{{{x},{y}}}' for x, y in pts[i:i + 8]) + ',')
    out += ['};', f'static const MapLine MAP_LINES[{len(lines)}] = {{']
    for i in range(0, len(lines), 6):
        out.append('  ' + ', '.join(f'{{{a},{b},{k}}}' for a, b, k in lines[i:i + 6]) + ',')
    out += ['};', f'static const MapCity MAP_CITIES[{len(cities)}] = {{']
    for name, lat, lon, pop in cities:
        out.append(f'  {{"{name}", {round((lon - LON0) / Q)}, {round((lat - LAT0) / Q)}, {pop}}},')
    out += ['};', '']
    dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'firmware', 'mapdata.h')
    open(dst, 'w', encoding='utf-8').write('\n'.join(out))
    print(f'{len(lines)} čar, {len(pts)} bodů ({len(pts) * 4} B), {len(cities)} měst')


main()
