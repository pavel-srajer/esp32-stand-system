#!/usr/bin/env python3
"""Vícejazyčný návod: docs/src/navod.cs.html (česky, zdroj) + docs/i18n/navod.<jazyk>.json
-> docs/navod.html (jedna stránka, všechny jazyky, přepínač nahoře).

Překládají se celé bloky (odstavce, položky, buňky, nadpisy, texty v SVG) i s vnitřním
značkováním a atributy aria-label/title/alt. Klíčem je český text bloku (se zúženými mezerami).

  python3 tools/make_docs.py            # sestaví stránku, vypíše chybějící překlady
  python3 tools/make_docs.py --strict   # chybějící překlad = chyba (CI)
  python3 tools/make_docs.py --dump     # vypíše české bloky jako JSON (pro překlad)
"""
import html, json, os, re, sys
from html.parser import HTMLParser

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SRC = os.path.join(ROOT, "docs", "src", "navod.cs.html")
OUT = os.path.join(ROOT, "docs", "navod.html")
I18N = os.path.join(ROOT, "docs", "i18n")
LANGS = [("cs", "Čeština"), ("en", "English"), ("de", "Deutsch"), ("it", "Italiano"),
         ("pt", "Português"), ("es", "Español"), ("vi", "Tiếng Việt")]
BLOCK = {"p", "li", "td", "th", "h1", "h2", "h3", "h4", "summary", "figcaption", "text", "button",
         "dt", "dd", "caption", "small", "label", "a", "span", "b", "strong", "div", "code", "em", "footer", "i"}
ATTRS = ("aria-label", "title", "alt", "placeholder")
VOID = {"br", "img", "meta", "link", "input", "hr", "source", "path", "circle", "rect", "line",
        "polyline", "polygon", "ellipse", "use", "stop"}
LETTER = re.compile(r"[A-Za-zÀ-ž]")


def norm(s):
    return re.sub(r"\s+", " ", s).strip()


class Tree(HTMLParser):
    """Najde elementy s pozicemi (začátek tagu, konec obsahu) v textu stránky."""
    def __init__(self, text):
        super().__init__(convert_charrefs=False)
        self.text = text
        self.lines = [0]
        for m in re.finditer("\n", text): self.lines.append(m.end())
        self.stack, self.els = [], []
    def off(self):
        l, c = self.getpos(); return self.lines[l - 1] + c
    def handle_starttag(self, tag, attrs):
        start = self.off(); end = start + len(self.get_starttag_text())
        el = {"tag": tag, "start": start, "inner": end, "attrs": attrs, "kids": [], "parent": self.stack[-1] if self.stack else None, "text": False}
        if self.stack: self.stack[-1]["kids"].append(el)
        self.els.append(el)
        if tag not in VOID: self.stack.append(el)
        else: el["close"] = el["end"] = end
    def handle_startendtag(self, tag, attrs):
        start = self.off(); end = start + len(self.get_starttag_text())
        el = {"tag": tag, "start": start, "inner": end, "close": end, "end": end, "attrs": attrs, "kids": [], "parent": self.stack[-1] if self.stack else None, "text": False}
        if self.stack: self.stack[-1]["kids"].append(el)
        self.els.append(el)
    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, -1, -1):
            if self.stack[i]["tag"] == tag:
                el = self.stack[i]; el["close"] = self.off(); el["end"] = self.text.index(">", el["close"]) + 1
                del self.stack[i:]; return
    def handle_data(self, data):
        if self.stack and LETTER.search(data): self.stack[-1]["text"] = True


def units(body):
    """Bloky k překladu: nejvnitřnější element, který přímo obsahuje text, a jehož
    předkové text přímo neobsahují (jinak se překládá celý předek i s vnořeným značkováním)."""
    t = Tree(body); t.feed(body)
    out = []
    def has_text_anc(el):
        p = el["parent"]
        while p is not None:
            if p["text"] and p["tag"] in BLOCK: return True
            p = p["parent"]
        return False
    for el in t.els:
        if el["tag"] in ("script", "style", "pre"): continue
        if el["text"] and el["tag"] in BLOCK and not has_text_anc(el) and "close" in el:
            inner = body[el["inner"]:el["close"]]
            if LETTER.search(html.unescape(re.sub(r"<[^>]+>", " ", inner))):
                out.append((el["inner"], el["close"], norm(inner)))
    # neobsloužený text (přímo v kontejneru mimo BLOCK) – pro kontrolu
    loose = [el for el in t.els if el["text"] and el["tag"] not in BLOCK and el["tag"] not in ("script", "style", "pre")]
    return out, loose, t


def split(src):
    i = src.index("</style>") + len("</style>")
    j = src.rindex("<script>")
    return src[:i], src[i:j], src[j:]


def translate_body(body, tr, lang, missing):
    us, _, tree = units(body)
    # atributy
    edits = []
    for el in tree.els:
        for k, v in el["attrs"]:
            if k in ATTRS and v and LETTER.search(v):
                key = norm(html.unescape(v))
                val = tr.get(key)
                if val is None: missing.add(key); continue
                tag = body[el["start"]:el["inner"]]
                new = re.sub(r'(\s%s=")([^"]*)(")' % re.escape(k), lambda m: m.group(1) + html.escape(val, quote=True) + m.group(3), tag, count=1)
                edits.append((el["start"], el["inner"], new))
    for a, b, key in us:
        val = tr.get(key)
        if val is None: missing.add(key); continue
        edits.append((a, b, val))
    # od konce, ať sedí pozice; atributové úpravy a obsah se nepřekrývají (tag vs. vnitřek)
    edits.sort(key=lambda e: e[0], reverse=True)
    out = body
    for a, b, new in edits: out = out[:a] + new + out[b:]
    # jedinečná id a odkazy v rámci jazyka
    if lang != "cs":
        out = re.sub(r'\bid="([^"]+)"', lambda m: 'id="%s-%s"' % (m.group(1), lang), out)
        out = re.sub(r'href="#([^"]+)"', lambda m: 'href="#%s-%s"' % (m.group(1), lang), out)
    return out


def keys_of(body):
    us, _, tree = units(body)
    ks = [k for _, _, k in us]
    for el in tree.els:
        for k, v in el["attrs"]:
            if k in ATTRS and v and LETTER.search(v): ks.append(norm(html.unescape(v)))
    seen, outk = set(), []
    for k in ks:
        if k not in seen: seen.add(k); outk.append(k)
    return outk


SWITCH_CSS = """
.langbar{position:sticky;top:0;z-index:20;display:flex;flex-wrap:wrap;gap:4px;justify-content:flex-end;padding:6px 12px;background:var(--paper);border-bottom:1px solid var(--line)}
.langbar button{font:500 13px var(--f-body);padding:4px 9px;border-radius:6px;border:1px solid var(--line);background:var(--panel);color:var(--ink);cursor:pointer}
.langbar button[aria-pressed="true"]{background:var(--blue);border-color:var(--blue);color:#fff}
.lng[hidden]{display:none}
.langbar .s{display:none}
@media (max-width:560px){.langbar{justify-content:center;gap:3px;padding:5px 6px}.langbar .f{display:none}.langbar .s{display:inline}.langbar button{padding:4px 7px}}
"""

SWITCH_JS = """
<script>
(function(){
  var L=%s, bar=document.querySelector('.langbar');
  function show(l){
    document.querySelectorAll('.lng').forEach(function(d){d.hidden=d.lang!==l});
    bar.querySelectorAll('button').forEach(function(b){b.setAttribute('aria-pressed',b.dataset.l===l)});
    document.documentElement.lang=l;
    var T={cs:'Radar letadel ESP32',en:'ESP32 aircraft radar',de:'ESP32 Flugzeugradar',it:'Radar aerei ESP32',pt:'Radar de aviões ESP32',es:'Radar de aviones ESP32',vi:'Radar máy bay ESP32'};
    if(T[l])document.title=T[l];
    try{localStorage.setItem('navod-lang',l)}catch(e){}
  }
  bar.addEventListener('click',function(e){var b=e.target.closest('button');if(b)show(b.dataset.l)});
  var pick=null;
  try{pick=localStorage.getItem('navod-lang')}catch(e){}
  if(L.indexOf(pick)<0){pick='en';var n=(navigator.languages||[navigator.language||'en']);
    for(var i=0;i<n.length;i++){var c=String(n[i]).slice(0,2).toLowerCase();if(L.indexOf(c)>=0){pick=c;break}}}
  show(pick);
})();
</script>
"""


def main():
    src = open(SRC, encoding="utf-8").read()
    head, body, script = split(src)
    if "--dump" in sys.argv:
        json.dump(keys_of(body), sys.stdout, ensure_ascii=False, indent=1); return
    _, loose, _ = units(body)
    if loose:
        print("Varování: text mimo překládané bloky:", ", ".join(sorted({e["tag"] for e in loose})))
    parts, total_missing = [], 0
    for code, name in LANGS:
        if code == "cs":
            out = body
        else:
            path = os.path.join(I18N, f"navod.{code}.json")
            tr = json.load(open(path, encoding="utf-8")) if os.path.exists(path) else {}
            missing = set()
            out = translate_body(body, tr, code, missing)
            if missing:
                total_missing += len(missing)
                print(f"{code}: chybí {len(missing)} překladů")
                for m in list(missing)[:5]: print("   ", m[:100])
        parts.append(f'<div class="lng" lang="{code}" hidden>{out}</div>')
    bar = '<nav class="langbar" aria-label="Language">' + "".join(
        f'<button type="button" data-l="{c}" lang="{c}" title="{n}"><span class="f">{n}</span><span class="s">{c.upper()}</span></button>' for c, n in LANGS) + "</nav>"
    page = "<!-- Vygenerováno tools/make_docs.py z docs/src/navod.cs.html + docs/i18n/*.json – needitovat ručně -->\n" + \
        head.replace("</style>", SWITCH_CSS + "</style>") + "\n" + bar + "\n" + "\n".join(parts) + "\n" + \
        script.replace("<script>", "<script>var COPY_TXT={cs:['Kopírovat','Zkopírováno'],en:['Copy','Copied'],de:['Kopieren','Kopiert'],it:['Copia','Copiato'],pt:['Copiar','Copiado'],es:['Copiar','Copiado'],vi:['Sao chép','Đã sao chép']};\n", 1) + \
        SWITCH_JS % json.dumps([c for c, _ in LANGS])
    open(OUT, "w", encoding="utf-8").write(page)
    print(f"docs/navod.html: {len(page) // 1024} kB, chybí celkem {total_missing}")
    if "--strict" in sys.argv and total_missing: sys.exit(1)


main()
