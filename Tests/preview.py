# Anteprima dell'interfaccia M-388 (stesso disegno di PluginEditor.cpp, vetro dei VU compreso).
# Uso: python3 Tests/preview.py  ->  preview.png (finestra intera) e preview_vu.gif (VU animati). Serve Pillow + numpy.
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
A = lambda f: os.path.join(ROOT, "Assets", f)
SS = 3                                                     # supersampling per bordi morbidi
W, H = 1380, 752
KNOBS = [(300, 317, 23, 0.0), (519, 317, 21, .5), (446, 462, 23, .5), (597, 462, 24, .5),
         (445, 610, 23, .5), (596, 610, 24, .5), (822, 324, 31, .5), (820, 516, 25, 0.0), (304, 578, 16, .5)]
FACES = [(714, 77, 917, 180, 815.5, 223.26, 118.45, 47, 103), (951, 77, 1154, 180, 1052.5, 223.26, 118.45, 47, 103)]
FADER = (291.5, 612.5, 1066.0, 1070.0)
FADER_DB = [10, 6, 3, 0, -3, -6, -10, -15, -20, -30, -40, -50, -60, -80]
MIC_BOX, BYP_BOX, TAPE_BOX = (286, 419, 340, 442), (794, 636, 846, 663), (975, 648, 1160, 714)

class Canvas:                                              # lavora solo su un riquadro box=(x0,y0,x1,y1) dell'immagine
    def __init__(self, img, box):
        self.img, self.box = img, box
        crop = img.crop(box)
        self.w, self.h = crop.size[0] * SS, crop.size[1] * SS
        self.c = np.asarray(crop.convert("RGB").resize((self.w, self.h), Image.BICUBIC)).astype(np.float32) / 255
        yy, xx = np.mgrid[0:self.h, 0:self.w].astype(np.float32)
        self.x, self.y = (xx + .5) / SS + box[0], (yy + .5) / SS + box[1]   # coordinate dell'immagine originale
        self.clip = np.ones((self.h, self.w), np.float32)
    def mask_rect(self, x0, y0, x1, y1):
        return ((self.x >= x0) & (self.x < x1) & (self.y >= y0) & (self.y < y1)).astype(np.float32)
    def mask_poly(self, pts):
        m = Image.new("L", (self.w, self.h)); ImageDraw.Draw(m).polygon([((px - self.box[0]) * SS, (py - self.box[1]) * SS) for px, py in pts], fill=255)
        return np.asarray(m).astype(np.float32) / 255
    def mask_ellipse(self, cx, cy, rx, ry):
        return (((self.x - cx) / rx) ** 2 + ((self.y - cy) / ry) ** 2 <= 1).astype(np.float32)
    def mask_line(self, ax, ay, bx, by, wd):
        dx, dy = bx - ax, by - ay; L = math.hypot(dx, dy) or 1; nx, ny = -dy / L * wd / 2, dx / L * wd / 2
        return self.mask_poly([(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)])
    def grad(self, p0, p1, stops):                          # gradiente lineare con fermate (t, (r,g,b,a)), bloccato agli estremi
        (ax, ay), (bx, by) = p0, p1; dx, dy = bx - ax, by - ay; d2 = dx * dx + dy * dy or 1
        t = np.clip(((self.x - ax) * dx + (self.y - ay) * dy) / d2, 0, 1)
        ts = [s[0] for s in stops]; out = np.zeros((self.h, self.w, 4), np.float32)
        for k in range(4): out[..., k] = np.interp(t, ts, [s[1][k] for s in stops])
        return out
    def fill(self, mask, rgba):
        if not isinstance(rgba, np.ndarray): rgba = np.array(rgba, np.float32)
        a = mask * self.clip * (rgba[..., 3] if rgba.ndim == 3 else rgba[3])
        col = rgba[..., :3] if rgba.ndim == 3 else rgba[:3]
        self.c = self.c * (1 - a[..., None]) + col * a[..., None]
    def commit(self):                                      # rimette il riquadro elaborato nell'immagine
        self.img.paste(Image.fromarray((np.clip(self.c, 0, 1) * 255).astype(np.uint8)).resize((self.w // SS, self.h // SS), Image.LANCZOS), self.box[:2])

def glass(cv, F):                                          # = drawGlass() in PluginEditor.cpp
    x0, y0, x1, y1 = F[:4]; w, h = x1 - x0, y1 - y0; K, Wt = (0, 0, 0), (1, 1, 1)
    def band(rect, p0, p1, col, a): cv.fill(cv.mask_rect(*rect), cv.grad(p0, p1, [(0, (*col, a)), (1, (*col, 0))]))
    band((x0, y0, x1, y0 + h * .16), (0, y0), (0, y0 + h * .16), K, .42)
    band((x0, y1 - h * .08, x1, y1), (0, y1), (0, y1 - h * .08), K, .22)
    band((x0, y0, x0 + w * .06, y1), (x0, 0), (x0 + w * .06, 0), K, .30)
    band((x1 - w * .06, y0, x1, y1), (x1, 0), (x1 - w * .06, 0), K, .30)
    ecx, ecy, rx, ry = x0 + w * .5, y0 - h * .55, w * .78, h * 1.0
    cv.fill(cv.mask_ellipse(ecx, ecy, rx, ry), cv.grad((0, y0), (0, ecy + ry), [(0, (*Wt, .17)), (1, (*Wt, .0))]))
    for a, b, c, d, al in ((.66, .74, .46, .54, .10), (.77, .80, .57, .60, .07)):
        pts = [(x0 + w * a, y0), (x0 + w * b, y0), (x0 + w * d, y1), (x0 + w * c, y1)]
        cv.fill(cv.mask_poly(pts), cv.grad((0, y0), (0, y1), [(0, (*Wt, al)), (1, (*Wt, 0))]))
    cv.fill(cv.mask_rect(x0 + 4, y1 - 2.6, x1 - 4, y1 - 1.3), cv.grad((x0, 0), (x1, 0), [(0, (*Wt, 0)), (.5, (*Wt, .38)), (1, (*Wt, 0))]))
    for r in ((x0, y0, x1, y0 + 1.2), (x0, y1 - 1.2, x1, y1), (x0, y0, x0 + 1.2, y1), (x1 - 1.2, y0, x1, y1)):
        cv.fill(cv.mask_rect(*r), (0, 0, 0, .35))

def vu(img, F, db, over, glass_on=True):
    x0, y0, x1, y1, cx, py, R, th, Hh = F
    cv = Canvas(img, (x0, y0, x1, y1))
    cv.clip = cv.mask_rect(x0, y0, x1, y1)
    if over > 0: cv.fill(cv.mask_rect(x0, y0, x1, y1), (1, .1, .05, .38 * over))
    a = math.radians(-th + 10 ** (db / 20) / 10 ** .15 * 2 * th)
    L = R + Hh * .03; t = (py - (y1 - 1)) / math.cos(a); wd = max(1, Hh * .012)
    bx, by, tx, ty = cx + t * math.sin(a), py - t * math.cos(a), cx + L * math.sin(a), py - L * math.cos(a)
    cv.fill(cv.mask_line(bx + 2, by + 2.5, tx + 2, ty + 2.5, wd), (0x28 / 255, 0x14 / 255, 0, 0x48 / 255))
    cv.fill(cv.mask_line(bx, by, tx, ty, wd), (0x1c / 255, 0x14 / 255, 0x10 / 255, 1))
    if glass_on: glass(cv, F)
    cv.commit()

def db_to_frac(db):
    for i in range(1, len(FADER_DB)):
        if db >= FADER_DB[i]: return ((i - 1) + (FADER_DB[i - 1] - db) / (FADER_DB[i - 1] - FADER_DB[i])) / (len(FADER_DB) - 1)
    return 1.0

# --- display a 14 segmenti anni '80 (= Source/SegmentFont.h) ---
SG = .12
SEGS = [(SG, 0, 1 - SG, 0), (1, SG, 1, 1 - SG), (1, 1 + SG, 1, 2 - SG), (SG, 2, 1 - SG, 2), (0, 1 + SG, 0, 2 - SG), (0, SG, 0, 1 - SG),
        (SG, 1, .5 - SG, 1), (.5 + SG, 1, 1 - SG, 1),
        (SG * 1.6, SG * 2, .5 - SG * 1.2, 1 - SG * 1.6), (.5, SG, .5, 1 - SG), (1 - SG * 1.6, SG * 2, .5 + SG * 1.2, 1 - SG * 1.6),
        (.5 - SG * 1.2, 1 + SG * 1.6, SG * 1.6, 2 - SG * 2), (.5, 1 + SG, .5, 2 - SG), (.5 + SG * 1.2, 1 + SG * 1.6, 1 - SG * 1.6, 2 - SG * 2)]
_B = {n: 1 << i for i, n in enumerate("a b c d e f g1 g2 h i j k l m".split())}
_GL = {"0": "a b c d e f j k", "1": "b c j", "2": "a b g1 g2 e d", "3": "a b c d g2", "4": "f g1 g2 b c", "5": "a f g1 g2 c d",
       "6": "a f e d c g1 g2", "7": "a b c", "8": "a b c d e f g1 g2", "9": "a b c d f g1 g2", "A": "a b c e f g1 g2",
       "B": "a b c d i l g2", "C": "a d e f", "D": "a b c d i l", "E": "a d e f g1", "F": "a e f g1", "G": "a c d e f g2",
       "H": "b c e f g1 g2", "I": "a d i l", "K": "e f g1 j m", "L": "d e f", "M": "b c e f h j", "N": "b c e f h m",
       "O": "a b c d e f", "P": "a b e f g1 g2", "Q": "a b c d e f m", "R": "a b e f g1 g2 m", "S": "a f g1 g2 c d", "T": "a i l",
       "U": "b c d e f", "J": "b c d e", "V": "e f k j", "W": "b c e f k m", "Y": "h j l", "X": "h j k m", "Z": "a d j k",
       "+": "g1 g2 i l", "-": "g1 g2", "%": "f c j k g1 g2"}
def seg_mask(ch): return sum(_B[n] for n in _GL.get(ch.upper(), "").split())
def seg_width(t, H, cellW=.56, gap=.30):
    n = sum(1 for ch in t if ch != "."); return n * H * (cellW + gap) - H * gap if n else 0
def seg_draw(d, t, cx, cy, H, col, ghost, S, slant=.14, thick=.11, cellW=.56, gap=.30):
    """disegna su ImageDraw RGBA con supersampling S; cx, cy = centro in pixel originali"""
    u = H / 2; x0 = cx - seg_width(t, H) / 2 - u * slant; y0 = cy - u; th = H * thick
    P = lambda c0, px, py: ((c0 + px * u * cellW * 2 + (2 - py) * u * slant) * S, (y0 + py * u) * S)
    def line(p, q, wd, rgba):
        d.line([p, q], fill=rgba, width=max(1, int(round(wd * S))))
        r = wd * S / 2
        for e in (p, q): d.ellipse((e[0] - r, e[1] - r, e[0] + r, e[1] + r), fill=rgba)
    c0 = x0; r, g, b, a = col
    for n, ch in enumerate(t):
        if ch == ".": continue
        mk = seg_mask(ch); dp = n + 1 < len(t) and t[n + 1] == "."
        for k, q in enumerate(SEGS):
            if not (mk >> k) & 1: line(P(c0, q[0], q[1]), P(c0, q[2], q[3]), th, (r, g, b, int(a * ghost)))
        for k, q in enumerate(SEGS):
            if (mk >> k) & 1: line(P(c0, q[0], q[1]), P(c0, q[2], q[3]), th * 2.6, (r, g, b, int(a * .16)))
        for k, q in enumerate(SEGS):
            if (mk >> k) & 1: line(P(c0, q[0], q[1]), P(c0, q[2], q[3]), th, (r, g, b, a))
        px, py = P(c0, 1 + gap * .5 / cellW, 2); rr = th * .7 * S
        d.ellipse((px - rr, py - rr, px + rr, py + rr), fill=(r, g, b, a if dp else int(a * ghost)))
        c0 += H * (cellW + gap)

def norm_freq(v, mn, mx):                                  # = NormalisableRange::setSkewForCentre(sqrt(mn*mx))
    skew = math.log(.5) / math.log((math.sqrt(mn * mx) - mn) / (mx - mn))
    return ((v - mn) / (mx - mn)) ** skew

def fmt_freq(v):                                           # = formatValue() in PluginProcessor.cpp
    return f"{v / 1000:.2f} kHz" if v >= 1000 else f"{round(v)} Hz"

def readout(img, cx, cy, label, value, accent, valcol=(0xff, 0xb5, 0x47)):           # = targhetta in M388Editor::paint (sopra il pomello)
    S = 4; value = value.upper()
    w, h = max(112, max(seg_width(value, 18), seg_width(label, 8.5)) + 30), 50
    x0 = min(max(4, cx - w / 2), W - w - 4); y0 = cy - h / 2
    pad = 8; box = (int(x0 - pad), int(y0 - pad), int(x0 + w + pad), int(y0 + h + pad))
    lay = Image.new("RGBA", ((box[2] - box[0]) * S, (box[3] - box[1]) * S), (0, 0, 0, 0))
    # le figure semitrasparenti si sovrappongono: si disegnano su strati separati e poi si compongono
    def layer(): return Image.new("RGBA", lay.size, (0, 0, 0, 0))
    ox, oy = x0 - box[0], y0 - box[1]
    l1 = layer(); d = ImageDraw.Draw(l1)
    d.rounded_rectangle(((ox + 1.5) * S, (oy + 2.5) * S, (ox + w + 1.5) * S, (oy + h + 2.5) * S), 7 * S, fill=(0, 0, 0, 89)); lay.alpha_composite(l1)
    l1 = layer(); d = ImageDraw.Draw(l1)
    d.rounded_rectangle((ox * S, oy * S, (ox + w) * S, (oy + h) * S), 7 * S, fill=(0x0d, 0x0b, 0x09, 242))
    d.rounded_rectangle(((ox + .75) * S, (oy + .75) * S, (ox + w - .75) * S, (oy + h - .75) * S), 7 * S, outline=(*accent, 230), width=int(1.5 * S))
    lay.alpha_composite(l1)
    for txt, H, ccy, col, gh in ((label, 8.5, oy + 3 + 9.5, (*accent, 255), .06), (value, 18, oy + 19 + (h - 24) / 2, (*valcol, 255), .07)):
        l1 = layer(); seg_draw(ImageDraw.Draw(l1), txt, ox + w / 2, ccy, H, col, gh, S); lay.alpha_composite(l1)
    lay = lay.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS)
    base = img.crop(box).convert("RGBA"); base.alpha_composite(lay); img.paste(base.convert("RGB"), box[:2])

def ch_window(img, text, col=(0xff, 0xb5, 0x47)):        # = finestrella del canale in M388Editor::paint
    S = 4; cx, cy = (TAPE_BOX[0] + TAPE_BOX[2]) / 2, (TAPE_BOX[1] + TAPE_BOX[3]) / 2; w, h = 112, 40
    box = (int(cx - w / 2 - 6), int(cy - h / 2 - 6), int(cx + w / 2 + 8), int(cy + h / 2 + 8))
    ox, oy = cx - w / 2 - box[0], cy - h / 2 - box[1]
    lay = Image.new("RGBA", ((box[2] - box[0]) * S, (box[3] - box[1]) * S), (0, 0, 0, 0))
    def layer(): return Image.new("RGBA", lay.size, (0, 0, 0, 0))
    l1 = layer(); ImageDraw.Draw(l1).rounded_rectangle(((ox + 1) * S, (oy + 2) * S, (ox + w + 1) * S, (oy + h + 2) * S), 5 * S, fill=(0, 0, 0, 77)); lay.alpha_composite(l1)
    l1 = layer(); d = ImageDraw.Draw(l1)
    d.rounded_rectangle((ox * S, oy * S, (ox + w) * S, (oy + h) * S), 5 * S, fill=(0x0d, 0x0b, 0x09, 240))
    d.rounded_rectangle(((ox + .6) * S, (oy + .6) * S, (ox + w - .6) * S, (oy + h - .6) * S), 5 * S, outline=(0x3a, 0x33, 0x2b, 255), width=int(1.2 * S))
    lay.alpha_composite(l1)
    l1 = layer(); seg_draw(ImageDraw.Draw(l1), text, ox + w / 2, oy + h / 2, 21, (*col, 255), .07, S); lay.alpha_composite(l1)
    lay = lay.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS)
    base = img.crop(box).convert("RGBA"); base.alpha_composite(lay); img.paste(base.convert("RGB"), box[:2])

CAL_BOX = (265, 172, 425, 206)
def cal_window(img, val="185"):                            # = finestrella TAPE LEVEL in M388Editor::paint
    S = 4; x0, y0, x1, y1 = CAL_BOX; w, h = x1 - x0, y1 - y0
    box = (x0 - 6, y0 - 6, x1 + 8, y1 + 8); ox, oy = x0 - box[0], y0 - box[1]
    lay = Image.new("RGBA", ((box[2] - box[0]) * S, (box[3] - box[1]) * S), (0, 0, 0, 0))
    def layer(): return Image.new("RGBA", lay.size, (0, 0, 0, 0))
    l1 = layer(); ImageDraw.Draw(l1).rounded_rectangle(((ox + 1) * S, (oy + 2) * S, (ox + w + 1) * S, (oy + h + 2) * S), 5 * S, fill=(0, 0, 0, 77)); lay.alpha_composite(l1)
    l1 = layer(); d = ImageDraw.Draw(l1)
    d.rounded_rectangle((ox * S, oy * S, (ox + w) * S, (oy + h) * S), 5 * S, fill=(0x0d, 0x0b, 0x09, 240))
    d.rounded_rectangle(((ox + .6) * S, (oy + .6) * S, (ox + w - .6) * S, (oy + h - .6) * S), 5 * S, outline=(0x3a, 0x33, 0x2b, 255), width=int(1.2 * S))
    lay.alpha_composite(l1)
    for txt, cx, H, col, gh in (("TAPE", ox + 4 + 31, 10, (0xd9, 0xd4, 0xca, 255), .06), (val, ox + 62 + 28, 18, (0xff, 0xb5, 0x47, 255), .07),
                                ("NWB", ox + 116 + (w - 116) / 2, 9, (0xd9, 0xd4, 0xca, 255), .06)):
        l1 = layer(); seg_draw(ImageDraw.Draw(l1), txt, cx, oy + h / 2, H, col, gh, S); lay.alpha_composite(l1)
    lay = lay.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS)
    base = img.crop(box).convert("RGBA"); base.alpha_composite(lay); img.paste(base.convert("RGB"), box[:2])

def render(db=(-3.0, -8.0), over=0.0, glass_on=True, knob_vals=None, show=None, cal="185"):
    bg = Image.open(A("bg_main.jpg")).convert("RGB")
    img = bg.copy()
    cap = Image.open(A("fader_cap.png")).convert("RGBA")
    y = FADER[0] + db_to_frac(0) * (FADER[1] - FADER[0])
    img.paste(cap, (int(FADER[3] - cap.width / 2), int(y - cap.height / 2)), cap)
    img.paste(Image.open(A("sw_mic_a.png")), MIC_BOX[:2]); img.paste(Image.open(A("sw_byp_a.png")), BYP_BOX[:2])
    for i, (cx, cy, rc, v) in enumerate(KNOBS):
        if knob_vals and i in knob_vals: v = knob_vals[i]
        cv = Canvas(img, (int(cx - rc - 2), int(cy - rc - 2), int(cx + rc + 3), int(cy + rc + 3)))
        a = math.radians(-135 + 270 * v)
        cv.fill(cv.mask_line(cx + 3 * math.sin(a), cy - 3 * math.cos(a), cx + (rc - 3) * math.sin(a), cy - (rc - 3) * math.cos(a), 2.6), (0xf4 / 255, 0xec / 255, 0xde / 255, 1))
        cv.commit()
    for i, F in enumerate(FACES): vu(img, F, db[i], over if i == 0 else 0, glass_on)
    out = img
    ch_window(out, "CH 1")
    cal_window(out, cal)
    if show:                                               # show = (indice pomello, etichetta, testo valore, colore)
        k, label, value, accent = show; cx, cy, rc, _ = KNOBS[k]
        readout(out, cx, cy - rc - 34, label, value, accent)
    return out

if __name__ == "__main__":
    out = os.environ.get("OUT", ROOT)
    render().save(os.path.join(out, "preview.png"))
    render(glass_on=False).crop((690, 60, 1180, 200)).save(os.path.join(out, "preview_vu_senza_vetro.png"))
    render().crop((690, 60, 1180, 200)).save(os.path.join(out, "preview_vu_con_vetro.png"))
    # targhetta delle frequenze EQ (pomello MID FREQ a 2,4 kHz e LOW FREQ a 120 Hz)
    mid = render(knob_vals={3: norm_freq(2400, 300, 10000)}, show=(3, "MID FREQ", fmt_freq(2400), (0x4f, 0x8f, 0xe0)))
    mid.save(os.path.join(out, "preview_eq_mid.png"))
    low = render(knob_vals={5: norm_freq(120, 40, 800)}, show=(5, "LOW FREQ", fmt_freq(120), (0x45, 0xb0, 0x5a)))
    low.save(os.path.join(out, "preview_eq_low.png"))
    for name, k, lab, val, acc, vv in (("preview_eq_mid_freq", 3, "MID FREQ", fmt_freq(2400), (0x4f, 0x8f, 0xe0), norm_freq(2400, 300, 10000)),
                                       ("preview_eq_low_freq", 5, "LOW FREQ", fmt_freq(120), (0x45, 0xb0, 0x5a), norm_freq(120, 40, 800)),
                                       ("preview_eq_mid_gain", 2, "MID GAIN", "+4.5 dB", (0x4f, 0x8f, 0xe0), .65),
                                       ("preview_input", 0, "INPUT GAIN", "+6.0 dB", (0xd8, 0x47, 0x3f), .6)):
        render(knob_vals={k: vv}, show=(k, lab, val, acc)).save(os.path.join(out, name + ".png"))
    frames = []
    for k in range(36):                                    # musica finta: il VU di saturazione pompa, il GR lo segue
        t = k / 36
        sat = -7 + 6.5 * abs(math.sin(2 * math.pi * t * 2)) + 1.5 * math.sin(2 * math.pi * t * 7)
        gr = 20 * math.log10(max(.1, min(1, (sat + 8) / 9) * .9 * 1.4125))
        ov = max(0.0, min(1.0, (sat + 1) / 4))
        frames.append(render((sat, gr), ov).crop((690, 60, 1180, 200)).resize((735, 210), Image.LANCZOS).quantize(128))
    frames[0].save(os.path.join(out, "preview_vu.gif"), save_all=True, append_images=frames[1:], duration=70, loop=0)
    print("ok")
