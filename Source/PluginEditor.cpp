#include "PluginEditor.h"
#include "BinaryData.h"
#include "SegmentFont.h"
#include <cmath>
#include <cstring>

using namespace juce;

static Rectangle<float> boxOf(const float* b, float pad = 0.f) { return { b[0] - pad, b[1] - pad, b[2] - b[0] + 2 * pad, b[3] - b[1] + 2 * pad }; }

// il fader ha 14 tacche equidistanti, ciascuna con il suo valore in dB (l'ultima = -80 = "-inf")
static float dbToFrac(float db) {
    for (int i = 1; i < FADER_N; ++i)
        if (db >= FADER_DB[i]) return ((float) (i - 1) + (FADER_DB[i - 1] - db) / (FADER_DB[i - 1] - FADER_DB[i])) / (FADER_N - 1);
    return 1.f;
}
static float fracToDb(float f) {
    const float p = jlimit(0.f, 1.f, f) * (FADER_N - 1);
    const int i = jmin((int) p, FADER_N - 2);
    return FADER_DB[i] + (FADER_DB[i + 1] - FADER_DB[i]) * (p - (float) i);
}

M388Editor::M388Editor(M388Processor& p) : AudioProcessorEditor(&p), proc(p) {
    bg = ImageCache::getFromMemory(BinaryData::bg_main_jpg, BinaryData::bg_main_jpgSize);
    spr = ImageCache::getFromMemory(BinaryData::fader_cap_png, BinaryData::fader_cap_pngSize);
    micOff = ImageCache::getFromMemory(BinaryData::sw_mic_a_png, BinaryData::sw_mic_a_pngSize);
    micOn = ImageCache::getFromMemory(BinaryData::sw_mic_b_png, BinaryData::sw_mic_b_pngSize);
    bypOff = ImageCache::getFromMemory(BinaryData::sw_byp_a_png, BinaryData::sw_byp_a_pngSize);
    bypOn = ImageCache::getFromMemory(BinaryData::sw_byp_b_png, BinaryData::sw_byp_b_pngSize);
    for (const auto& k : KNOBS) ctl.push_back({ &k, p.apvts.getParameter(k.pid) });
    chPar = p.apvts.getParameter("channel");
    calPar = p.apvts.getParameter("tape_cal");
    faderPar = p.apvts.getParameter("main_fader");
    bypassPar = p.apvts.getParameter("main_bypass");
    micPar = p.apvts.getParameter("main_micline");
    lastVals.assign((size_t) p.getParameters().size(), -1.f);
    setOpaque(true);
    setSize(1242, 677);
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio((double) IMG_W / IMG_H);
    setResizeLimits(460, 251, 2760, 1504);
    startTimerHz(30);
}

Rectangle<int> M388Editor::toScreen(Rectangle<float> r) const {
    return r.transformedBy(AffineTransform::scale(scale())).getSmallestIntegerContainer().expanded(2);
}

// Ridisegno mirato: tutta la finestra solo quando cambia un parametro (mouse o automazione), altrimenti solo i due VU.
void M388Editor::touch(int idx) {
    shown = idx; shownAlpha = 1.f; touchMs = Time::getMillisecondCounter();
}

void M388Editor::timerCallback() {
    // la targhetta resta 1 s dopo l'ultimo tocco, poi svanisce in circa 0,3 s
    if (shown >= 0 && dragIdx < 0 && !dragFader && Time::getMillisecondCounter() - touchMs > 1000) {
        shownAlpha -= .11f;
        if (shownAlpha <= 0.f) { shownAlpha = 0.f; shown = -1; }
        repaint(); return;
    }
    bool changed = false;
    const auto& pars = proc.getParameters();
    for (int i = 0; i < pars.size(); ++i) {
        const float v = pars[i]->getValue();
        if (v != lastVals[(size_t) i]) { lastVals[(size_t) i] = v; changed = true; }
    }
    if (changed) { repaint(); return; }
    const float v[2] = { proc.vu[0].load(), proc.vu[1].load() }, ov = proc.over.load();
    for (int i = 0; i < 2; ++i) {
        if (std::abs(v[i] - lastVu[i]) > .03f || (i == 0 && std::abs(ov - lastOver) > .01f)) {
            lastVu[i] = v[i]; if (i == 0) lastOver = ov;
            const auto& F = FACES[i];
            repaint(toScreen({ F.x0, F.y0, F.x1 - F.x0, F.y1 - F.y0 }));
        }
    }
}

// Vetro del VU (disegnato sopra quadrante e ago, gia' ritagliato sul quadrante):
// profondita' della cornice, riflesso curvo in alto, due strisce speculari, filo di luce sul bordo basso.
// I valori sono gli stessi di Tests/preview.py, che genera l'anteprima.
// etichetta e colore della targhetta per ogni controllo (colore = colore del pomello)
struct ReadoutStyle { const char* pid; const char* label; uint32 accent; };
static const ReadoutStyle STYLES[] = {
    { "main_input", "INPUT GAIN", 0xffd8473f }, { "main_high", "HIGH EQ", 0xffe58a2c },
    { "main_mid_gain", "MID GAIN", 0xff4f8fe0 }, { "main_mid_freq", "MID FREQ", 0xff4f8fe0 },
    { "main_low_gain", "LOW GAIN", 0xff45b05a }, { "main_low_freq", "LOW FREQ", 0xff45b05a },
    { "main_pan", "PAN", 0xffd9d4ca }, { "main_comp", "COMP", 0xffd9d4ca }, { "main_fader", "FADER", 0xffd9d4ca },
    { "main_hiss", "HISS", 0xffd9d4ca } };
static const ReadoutStyle& styleFor(const char* pid) {
    for (const auto& st : STYLES) if (std::strcmp(st.pid, pid) == 0) return st;
    return STYLES[8];
}

static void drawGlass(Graphics& g, const FaceDef& F) {
    const float x0 = F.x0, y0 = F.y0, w = F.x1 - F.x0, h = F.y1 - F.y0, x1 = F.x1, y1 = F.y1;
    const auto clear = Colours::black.withAlpha(0.f);
    // 1) ombra interna: il vetro e' incassato nella cornice
    g.setGradientFill(ColourGradient(Colours::black.withAlpha(.42f), 0, y0, clear, 0, y0 + h * .16f, false));
    g.fillRect(x0, y0, w, h * .16f);
    g.setGradientFill(ColourGradient(Colours::black.withAlpha(.22f), 0, y1, clear, 0, y1 - h * .08f, false));
    g.fillRect(x0, y1 - h * .08f, w, h * .08f);
    g.setGradientFill(ColourGradient(Colours::black.withAlpha(.30f), x0, 0, clear, x0 + w * .06f, 0, false));
    g.fillRect(x0, y0, w * .06f, h);
    g.setGradientFill(ColourGradient(Colours::black.withAlpha(.30f), x1, 0, clear, x1 - w * .06f, 0, false));
    g.fillRect(x1 - w * .06f, y0, w * .06f, h);
    // 2) riflesso curvo: grande ellisse sopra il quadrante, il bordo inferiore disegna un arco sul vetro
    {
        const float ecx = x0 + w * .5f, ecy = y0 - h * .55f, rx = w * .78f, ry = h * 1.0f;
        Path p; p.addEllipse(ecx - rx, ecy - ry, 2 * rx, 2 * ry);
        g.setGradientFill(ColourGradient(Colours::white.withAlpha(.17f), 0, y0, Colours::white.withAlpha(.0f), 0, ecy + ry, false));
        g.fillPath(p);
    }
    // 3) strisce speculari diagonali
    auto streak = [&](float a, float b, float c, float d, float alpha) {
        Path p;
        p.startNewSubPath(x0 + w * a, y0); p.lineTo(x0 + w * b, y0); p.lineTo(x0 + w * d, y1); p.lineTo(x0 + w * c, y1); p.closeSubPath();
        g.setGradientFill(ColourGradient(Colours::white.withAlpha(alpha), 0, y0, Colours::white.withAlpha(0.f), 0, y1, false));
        g.fillPath(p);
    };
    streak(.66f, .74f, .46f, .54f, .10f);
    streak(.77f, .80f, .57f, .60f, .07f);
    // 4) filo di luce sul bordo basso del vetro, piu' intenso al centro
    ColourGradient edge(Colours::white.withAlpha(0.f), x0, 0, Colours::white.withAlpha(0.f), x1, 0, false);
    edge.addColour(.5, Colours::white.withAlpha(.38f));
    g.setGradientFill(edge);
    g.fillRect(x0 + 4.f, y1 - 2.6f, w - 8.f, 1.3f);
    // 5) bordo sottile scuro del vetro
    g.setColour(Colours::black.withAlpha(.35f));
    g.drawRect(x0, y0, w, h, 1.2f);
}

void M388Editor::paint(Graphics& g) {
    g.drawImage(bg, getLocalBounds().toFloat());
    g.addTransform(AffineTransform::scale(scale()));
    const bool bypassed = bypassPar->getValue() > .5f;
    for (const auto& c : ctl) {
        const float a = (-135.f + 270.f * c.par->getValue()) * MathConstants<float>::pi / 180.f, cx = c.def->cx, cy = c.def->cy, rc = c.def->cap;
        g.setColour(Colour(0xfff4ecde));
        g.drawLine(cx + 3 * std::sin(a), cy - 3 * std::cos(a), cx + (rc - 3) * std::sin(a), cy - (rc - 3) * std::cos(a), 2.6f);
    }
    const float y = FADER.top + dbToFrac(faderPar->convertFrom0to1(faderPar->getValue())) * (FADER.bot - FADER.top);
    g.drawImageAt(spr, (int) (FADER.capCx - spr.getWidth() / 2.f), (int) (y - spr.getHeight() / 2.f));
    // levetta MIC/LINE e BYPASS: sprite originale = LINE / attivo, specchiata = MIC / escluso
    const bool mic = micPar->getValue() > .5f;
    g.drawImageAt(mic ? micOn : micOff, (int) MIC_BOX[0], (int) MIC_BOX[1]);
    g.drawImageAt(bypassed ? bypOn : bypOff, (int) BYP_BOX[0], (int) BYP_BOX[1]);
    if (bypassed) { g.setColour(Colour(0xe6221f19)); g.fillEllipse(LED_POS[0] - 12.f, LED_POS[1] - 12.f, 24.f, 24.f); }
    // numero del canale: finestrella con display anni '80 sull'etichetta del nastro, fra le due frecce
    {
        const auto win = Rectangle<float>(112.f, 40.f).withCentre(boxOf(TAPE_BOX).getCentre());
        g.setColour(Colours::black.withAlpha(.30f)); g.fillRoundedRectangle(win.translated(1.f, 2.f), 5.f);
        g.setColour(Colour(0xff0d0b09).withAlpha(.94f)); g.fillRoundedRectangle(win, 5.f);
        g.setColour(Colour(0xff3a332b)); g.drawRoundedRectangle(win.reduced(.6f), 5.f, 1.2f);
        seg::draw(g, "CH " + String((int) std::lround(chPar->getValue() * 7) + 1), win, seg::Style { 21.f }, Colour(0xffffb547), .07f);
    }
    // livello operativo del nastro (TAPE LEVEL): clic = valore successivo, Shift+clic = precedente
    {
        const auto win = boxOf(CAL_BOX);
        static const char* const CAL_TXT[4] = { "185", "250", "320", "500" };
        const int ci = jlimit(0, 3, (int) std::lround(calPar->getValue() * 3));
        g.setColour(Colours::black.withAlpha(.30f)); g.fillRoundedRectangle(win.translated(1.f, 2.f), 5.f);
        g.setColour(Colour(0xff0d0b09).withAlpha(.94f)); g.fillRoundedRectangle(win, 5.f);
        g.setColour(Colour(0xff3a332b)); g.drawRoundedRectangle(win.reduced(.6f), 5.f, 1.2f);
        seg::draw(g, "TAPE", win.withWidth(62.f).translated(4.f, 0.f), seg::Style { 10.f }, Colour(0xffd9d4ca), .06f);
        seg::draw(g, String(CAL_TXT[ci]), win.withTrimmedLeft(62.f).withWidth(56.f), seg::Style { 18.f }, Colour(0xffffb547), .07f);
        seg::draw(g, "NWB", win.withTrimmedLeft(116.f), seg::Style { 9.f }, Colour(0xffd9d4ca), .06f);
    }
    // VU: ago + rosso in sovraccarico + riflesso del vetro
    const float db[2] = { bypassed ? -22.f : proc.vu[0].load(), bypassed ? -22.f : proc.vu[1].load() }, over = bypassed ? 0.f : proc.over.load();
    for (int i = 0; i < 2; ++i) {
        const FaceDef& F = FACES[i];
        Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion((int) F.x0, (int) F.y0, (int) (F.x1 - F.x0), (int) (F.y1 - F.y0));
        const float red = i == 0 ? over : 0.f;      // il meter sinistro (saturazione) diventa rosso; il destro (compressione) no
        if (red > 0.f) { g.setColour(Colour::fromFloatRGBA(1.f, .1f, .05f, .38f * red)); g.fillRect(F.x0, F.y0, F.x1 - F.x0, F.y1 - F.y0); }
        const float a = (-F.th + std::pow(10.f, db[i] / 20.f) / std::pow(10.f, .15f) * 2 * F.th) * MathConstants<float>::pi / 180.f;
        const float L = F.R + F.H * .03f, t = (F.py - (F.y1 - 1.f)) / std::cos(a);
        const Point<float> base(F.cx + t * std::sin(a), F.py - t * std::cos(a)), tip(F.cx + L * std::sin(a), F.py - L * std::cos(a));
        const float w = std::max(1.f, F.H * .012f);
        g.setColour(Colour(0x48281400)); g.drawLine(base.x + 2, base.y + 2.5f, tip.x + 2, tip.y + 2.5f, w);
        g.setColour(Colour(0xff1c1410)); g.drawLine(base.x, base.y, tip.x, tip.y, w);
        drawGlass(g, F);                            // il vetro sta sopra l'ago: anche l'ago prende il riflesso
    }
    // targhetta con il valore (frequenze EQ comprese): compare quando tocchi un controllo e svanisce dopo il rilascio
    if (shown >= 0 && shownAlpha > 0.f) {
        const bool isFader = shown == (int) ctl.size();
        auto* par = isFader ? faderPar : ctl[(size_t) shown].par;
        const auto& st = isFader ? STYLES[8] : styleFor(ctl[(size_t) shown].def->pid);
        const String value = par->getCurrentValueAsText().toUpperCase();
        const seg::Style sl { 8.5f }, sv { 18.f };              // etichetta piccola, valore grande (display anni '80)
        const float w = jmax(112.f, jmax(seg::width(value, sv), seg::width(st.label, sl)) + 30.f), h = 50.f;
        const Point<float> c = isFader ? Point<float>(FADER.slot - 62.f - w / 2.f, y)
                                       : Point<float>(ctl[(size_t) shown].def->cx, ctl[(size_t) shown].def->cy - ctl[(size_t) shown].def->cap - 34.f);
        auto r = Rectangle<float>(w, h).withCentre(c);
        r = r.withX(jlimit(4.f, IMG_W - w - 4.f, r.getX()));
        const Colour accent(st.accent);
        g.setColour(Colours::black.withAlpha(.35f * shownAlpha)); g.fillRoundedRectangle(r.translated(1.5f, 2.5f), 7.f);
        g.setColour(Colour(0xff0d0b09).withAlpha(.95f * shownAlpha)); g.fillRoundedRectangle(r, 7.f);
        g.setColour(accent.withAlpha(.9f * shownAlpha)); g.drawRoundedRectangle(r.reduced(.75f), 7.f, 1.5f);
        seg::draw(g, st.label, r.withHeight(19.f).translated(0.f, 3.f), sl, accent.withAlpha(shownAlpha), .06f);
        seg::draw(g, value, r.withTrimmedTop(19.f).withTrimmedBottom(5.f), sv, Colour(0xffffb547).withAlpha(shownAlpha), .07f);
    }
}

int M388Editor::hitKnob(Point<float> pt) const {
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int) ctl.size(); ++i) {
        const float d = std::hypot(pt.x - ctl[(size_t) i].def->cx, pt.y - ctl[(size_t) i].def->cy);
        if (d < bd) { bd = d; best = i; }
    }
    return bd <= 62.f ? best : -1;
}
bool M388Editor::hitFader(Point<float> pt) { return std::abs(pt.x - FADER.slot) < 45.f && pt.y > FADER.top - 30.f && pt.y < FADER.bot + 30.f; }

void M388Editor::mouseDown(const MouseEvent& e) {
    const auto pt = e.position / scale();
    if (boxOf(TAPE_BOX, 4.f).contains(pt)) {
        const int idx = (int) std::lround(chPar->getValue() * 7), n = (idx + (pt.x < boxOf(TAPE_BOX).getCentreX() ? 7 : 1)) % 8;
        chPar->beginChangeGesture(); chPar->setValueNotifyingHost(n / 7.f); chPar->endChangeGesture();
        return;
    }
    if (boxOf(CAL_BOX, 4.f).contains(pt)) {
        const int ci = (int) std::lround(calPar->getValue() * 3), n = (ci + (e.mods.isShiftDown() ? 3 : 1)) % 4;
        calPar->beginChangeGesture(); calPar->setValueNotifyingHost(n / 3.f); calPar->endChangeGesture();
        return;
    }
    if (boxOf(MIC_BOX, 8.f).contains(pt)) {
        micPar->beginChangeGesture(); micPar->setValueNotifyingHost(micPar->getValue() > .5f ? 0.f : 1.f); micPar->endChangeGesture();
        return;
    }
    if (boxOf(BYP_BOX, 8.f).contains(pt)) {
        bypassPar->beginChangeGesture(); bypassPar->setValueNotifyingHost(bypassPar->getValue() > .5f ? 0.f : 1.f); bypassPar->endChangeGesture();
        return;
    }
    if (hitFader(pt)) {
        dragFader = true; touch((int) ctl.size()); faderPar->beginChangeGesture();
        faderDb0 = faderPar->convertFrom0to1(faderPar->getValue()); dragY0 = pt.y; repaint(); return;
    }
    const int k = hitKnob(pt);
    if (k < 0) return;
    touch(k);
    dragIdx = k; lastFine = e.mods.isShiftDown(); dragStart = ctl[(size_t) k].par->getValue(); dragY0 = pt.y; ctl[(size_t) k].par->beginChangeGesture(); repaint();
}

void M388Editor::mouseDrag(const MouseEvent& e) {
    const auto pt = e.position / scale();
    const bool fine = e.mods.isShiftDown();
    if (dragFader || dragIdx >= 0) touch(dragFader ? (int) ctl.size() : dragIdx);
    if (dragFader) {   // movimento relativo e "duro": 100 px di mouse = ~35 px di corsa (Shift = ancora piu' fine)
        const float ratio = fine ? .08f : .35f;
        const float f = jlimit(0.f, 1.f, dbToFrac(faderDb0) + (pt.y - dragY0) / (FADER.bot - FADER.top) * ratio);
        faderPar->setValueNotifyingHost(faderPar->convertTo0to1(fracToDb(f)));
        return;
    }
    if (dragIdx < 0) return;
    // Shift = regolazione fine; il punto di partenza si riallinea quando si preme/rilascia Shift, senza salti
    auto* par = ctl[(size_t) dragIdx].par;
    if (fine != lastFine) { dragStart = par->getValue(); dragY0 = pt.y; lastFine = fine; }
    par->setValueNotifyingHost(jlimit(0.f, 1.f, dragStart + (dragY0 - pt.y) / (fine ? 850.f : 170.f)));
}

// doppio clic: fader = 0 dB, manopola = valore iniziale
void M388Editor::mouseDoubleClick(const MouseEvent& e) {
    const auto pt = e.position / scale();
    if (hitFader(pt)) {
        const bool inGesture = dragFader;
        if (!inGesture) faderPar->beginChangeGesture();
        faderPar->setValueNotifyingHost(faderPar->convertTo0to1(0.f));
        if (!inGesture) faderPar->endChangeGesture();
        faderDb0 = 0.f; dragY0 = pt.y; touch((int) ctl.size());
        return;
    }
    const int k = hitKnob(pt);
    if (k < 0) return;
    auto* par = ctl[(size_t) k].par;
    const bool inGesture = dragIdx == k;
    if (!inGesture) par->beginChangeGesture();
    par->setValueNotifyingHost(par->getDefaultValue());
    if (!inGesture) par->endChangeGesture();
    if (inGesture) { dragStart = par->getDefaultValue(); dragY0 = pt.y; }
    touch(k);
}

void M388Editor::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w) {
    if (dragFader || dragIdx >= 0) return;
    const auto pt = e.position / scale();
    const float step = (w.isReversed ? -w.deltaY : w.deltaY) * (e.mods.isShiftDown() ? .01f : .05f);
    if (hitFader(pt)) {
        const float f = jlimit(0.f, 1.f, dbToFrac(faderPar->convertFrom0to1(faderPar->getValue())) - step);
        faderPar->beginChangeGesture(); faderPar->setValueNotifyingHost(faderPar->convertTo0to1(fracToDb(f))); faderPar->endChangeGesture();
        touch((int) ctl.size());
        return;
    }
    const int k = hitKnob(pt);
    if (k < 0) return;
    auto* par = ctl[(size_t) k].par;
    par->beginChangeGesture(); par->setValueNotifyingHost(jlimit(0.f, 1.f, par->getValue() + step)); par->endChangeGesture();
    touch(k);
}

void M388Editor::mouseUp(const MouseEvent&) {
    if (dragFader) faderPar->endChangeGesture();
    if (dragIdx >= 0) ctl[(size_t) dragIdx].par->endChangeGesture();
    dragFader = false; dragIdx = -1;
    repaint();
}
