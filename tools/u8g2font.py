# Dekodér/enkodér písma u8g2 (formát podle LovyanGFX lgfx_fonts.cpp, U8g2font::drawChar).
# Dekodér/enkodér písma u8g2 (formát podle LovyanGFX lgfx_fonts.cpp)
class Bits:
    def __init__(s, b, pos=0): s.b, s.bit, s.p = b, 0, pos
    def get(s, n):
        v = 0
        for i in range(n):
            byte = s.b[s.p]
            v |= ((byte >> s.bit) & 1) << i
            s.bit += 1
            if s.bit == 8: s.bit, s.p = 0, s.p + 1
        return v
    def sget(s, n): return s.get(n) - (1 << (n - 1))

class W:
    def __init__(s): s.out, s.cur, s.bit = bytearray(), 0, 0
    def put(s, v, n):
        assert 0 <= v < (1 << n), (v, n)
        for i in range(n):
            s.cur |= ((v >> i) & 1) << s.bit
            s.bit += 1
            if s.bit == 8: s.out.append(s.cur); s.cur, s.bit = 0, 0
    def sput(s, v, n): s.put(v + (1 << (n - 1)), n)
    def done(s):
        if s.bit: s.out.append(s.cur)
        return bytes(s.out)

class Font:
    def __init__(s, b):
        s.b = b
        s.h = b[:23]
        s.b0, s.b1, s.bw, s.bh, s.bx, s.by, s.bd = b[2], b[3], b[4], b[5], b[6], b[7], b[8]
        upA = (b[17] << 8) | b[18]; lowa = (b[19] << 8) | b[20]; uni = (b[21] << 8) | b[22]
        s.glyphs = {}  # enc -> (w,h,x,y,dx,rows)
        p = 23
        while b[p + 1]:
            s.glyphs[b[p]] = s.decode(p + 2); p += b[p + 1]
        s.low_end = p  # konec části <256 (bajt se size 0)
        p = 23 + uni
        while True:
            enc = (b[p + 2] << 8) | b[p + 3]; p += 4
            if enc == 0xFFFF: break
        while p + 3 <= len(b):
            enc = (b[p] << 8) | b[p + 1]
            if enc == 0: break
            s.glyphs[enc] = s.decode(p + 3); p += b[p + 2]
    def decode(s, pos):
        r = Bits(s.b, pos)
        w, h = r.get(s.bw), r.get(s.bh)
        x, y, dx = r.sget(s.bx), r.sget(s.by), r.sget(s.bd)
        px = []
        if w:
            while len(px) < w * h:
                a, bb = r.get(s.b0), r.get(s.b1)
                while True:
                    px += [0] * a + [1] * bb
                    if not r.get(1): break
        rows = [px[i * w:(i + 1) * w] for i in range(h)] if w else []
        return (w, h, x, y, dx, rows)
    def encode(s, g):
        w, h, x, y, dx, rows = g
        o = W()
        o.put(w, s.bw); o.put(h, s.bh); o.sput(x, s.bx); o.sput(y, s.by); o.sput(dx, s.bd)
        if w:
            px = [v for r in rows for v in r]
            runs, i = [], 0  # dvojice (nuly, jedničky)
            while i < len(px):
                a = 0
                while i < len(px) and px[i] == 0: a += 1; i += 1
                bb = 0
                while i < len(px) and px[i] == 1: bb += 1; i += 1
                runs.append((a, bb))
            m0, m1 = (1 << s.b0) - 1, (1 << s.b1) - 1
            pairs = []
            for a, bb in runs:
                while a > m0: pairs.append((m0, 0)); a -= m0
                while bb > m1: pairs.append((a, m1)); a = 0; bb -= m1
                pairs.append((a, bb))
            for a, bb in pairs:
                o.put(a, s.b0); o.put(bb, s.b1); o.put(0, 1)
        return o.done()

def to_abs(g):  # množina pixelů (sloupec, řádek nad účaří kladně dolů: yy = -(y+h)+r)
    w, h, x, y, dx, rows = g
    return {(x + c, -(y + h) + r) for r in range(h) for c in range(w) if rows[r][c]}

def from_abs(pix, dx):
    if not pix: return (0, 0, 0, 0, dx, [])
    xs = [p[0] for p in pix]; ys = [p[1] for p in pix]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    w, h = x1 - x0 + 1, y1 - y0 + 1
    rows = [[1 if (x0 + c, y0 + r) in pix else 0 for c in range(w)] for r in range(h)]
    return (w, h, x0, -(y0 + h), dx, rows)

def show(pix):
    if not pix: return ""
    xs = [p[0] for p in pix]; ys = [p[1] for p in pix]
    return "\n".join("".join("#" if (c, r) in pix else "." for c in range(min(xs), max(xs) + 1)) + ("  <- účaří" if r == -1 else "") for r in range(min(ys), max(ys) + 1))
