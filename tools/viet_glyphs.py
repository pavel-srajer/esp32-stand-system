# Vietnamská písmena pro displej: efont je nemá (ạ ả ấ … ơ ư Đ), skládají se ze
# základních písmen a značek z téhož písma (čárky z á, à, ã, â), dấu hỏi, tečka dole
# a rožek ơ/ư se domalují podle tloušťky tahu.
import unicodedata as ud
from u8g2font import to_abs, from_abs

GRAVE, ACUTE, TILDE, HOOK, DOT, CIRC, BREVE, HORN = 0x300, 0x301, 0x303, 0x309, 0x323, 0x302, 0x306, 0x31B
ABOVE = {GRAVE, ACUTE, TILDE, HOOK}

def vi_chars():
    out = set()
    for base in "aăâeêioôơuưyAĂÂEÊIOÔƠUƯY":
        for tone in [None, GRAVE, ACUTE, HOOK, TILDE, DOT]:
            s = ud.normalize("NFC", base + (chr(tone) if tone else ""))
            if len(s) == 1: out.add(ord(s))
    return sorted(out | {0x110, 0x111})

def bbox(p):
    xs = [a for a, _ in p]; ys = [b for _, b in p]
    return min(xs), max(xs), min(ys), max(ys)

def compose(font, cp, size):
    g = font.glyphs
    if cp in g: return None  # už v písmu je
    if cp == 0x110:  # Đ = D s vodorovnou čárkou přes levý tah (efont ho nemá)
        d = set(to_abs(g[ord("D")])); stem = stem_width(font)
        x0, x1, y0, y1 = bbox(d)
        ym = (y0 + y1) // 2
        bar = {(x, ym + j) for x in range(x0 - 1, x0 + stem + 1 + (1 if size >= 16 else 0)) for j in range(max(1, stem // 2 + (stem > 1)))}
        w, h, x, y, dx, rows = g[ord("D")]
        return from_abs(d | bar, dx + (1 if x0 - 1 < 0 else 0)) if x0 - 1 >= 0 else from_abs({(a + 1, b) for a, b in d | bar}, dx + 1)
    nfd = ud.normalize("NFD", chr(cp))
    # značky písmene (stříška, oblouček, rožek) před tóny – Unicode řadí tečku dole před stříšku
    nfd = nfd[0] + "".join(sorted(nfd[1:], key=lambda c: 0 if ord(c) in (CIRC, BREVE, HORN) else 1))
    # nejdelší začátek, který v písmu existuje jako jeden znak
    for k in range(len(nfd), 0, -1):
        pre = ud.normalize("NFC", nfd[:k])
        if len(pre) != 1: continue
        if len(pre) == 1 and ord(pre) in g: break
    else:
        return None
    pix = set(to_abs(g[ord(pre)])); dx = g[ord(pre)][4]
    base = nfd[0]; upper = base.isupper()
    letter = set(to_abs(g[ord(base)]))  # samotné písmeno bez značek
    stem = stem_width(font)
    for m in map(ord, nfd[k:]):
        if m in ABOVE:
            mk = mark(font, m, upper, stem)
            mx0, mx1, my0, my1 = bbox(mk)
            x0, x1, y0, y1 = bbox(pix)
            lx0, lx1, _, _ = bbox(letter)
            gap = 1
            if pix == letter or ord(pre) in (ord("ı"),):  # značka přímo na písmeno: mezera jako v á
                gap = natural_gap(font, upper)
            cx = (lx0 + lx1) / 2 - (mx0 + mx1) / 2
            ox = round(cx + 0.01)
            oy = (y0 - gap - 1) - my1
            pix |= {(a + ox, b + oy) for a, b in mk}
        elif m == DOT:
            x0, x1, y0, y1 = bbox(letter)
            lx0, lx1, _, _ = bbox(letter)
            # tečka pod účařím (pod písmenem bez dolní dotahy), uprostřed písmene
            bottom = max(b for a, b in letter if True)
            cx = round((lx0 + lx1) / 2 - (stem - 1) / 2)
            yb = bottom + 2
            pix |= {(cx + i, yb + j) for i in range(stem) for j in range(stem)}
        elif m == HORN:
            x0, x1, y0, y1 = bbox(letter)
            top = [a for a, b in letter if b == y0]
            right_top = max(top)
            h = max(2, stem + 1) if size >= 16 else 2
            # rožek: šikmý tah od pravého horního rohu nahoru doprava
            sx = x1 + 1 if right_top < x1 else x1 + 1
            pts = set()
            if right_top < x1:  # o, O: horní řádek nekončí v pravém sloupci -> napojit
                for i in range(stem): pts.add((x1 - i + 0, y0))
            for i in range(stem):
                pts.add((sx + i - 1 + 1, y0 - 1)) if h >= 2 else None
                if h >= 3: pts.add((sx + i, y0 - 2))
            pix |= pts
            letter = letter | pts
            dx = max(dx, max(a for a, _ in pix) + 1 + (1 if size >= 16 else 0))
        else:
            return None
    return from_abs(pix, dx)

def stem_width(font):
    w, h, x, y, dx, rows = font.glyphs[ord("l")]
    mid = rows[len(rows) // 2]
    return max(1, sum(mid))

def natural_gap(font, upper):
    a, aa = ("A", "Á") if upper else ("a", "á")
    b = to_abs(font.glyphs[ord(a)]); c = to_abs(font.glyphs[ord(aa)])
    m = c - b
    return bbox(b)[2] - bbox(m)[3] - 1

def mark(font, m, upper, stem):
    src = {GRAVE: "àÀ", ACUTE: "áÁ", TILDE: "ãÃ"}
    if m in src:
        ch = src[m][1 if upper else 0]; base = "A" if upper else "a"
        return to_abs(font.glyphs[ord(ch)]) - to_abs(font.glyphs[ord(base)])
    # dấu hỏi (háček jako otazník bez tečky), výška podle čárky
    ac = mark(font, ACUTE, upper, stem)
    hh = bbox(ac)[3] - bbox(ac)[2] + 1
    pat = {1: ["##"], 2: ["##", ".#"], 3: ["##.", "..#", ".#."], 4: [".##.", "...#", "..#.", "..#."],
           5: [".###.", "....#", "...#.", "..#..", "..#.."]}[min(max(hh, 2) + (1 if hh <= 2 else 0), 5)]
    pts = {(c, r) for r, row in enumerate(pat) for c, ch in enumerate(row) if ch == "#"}
    if stem > 1: pts |= {(c + 1, r) for c, r in pts}
    return pts
