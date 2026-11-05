// M-388 tape core: DSP senza dipendenze (header-only).
// Catena: ingresso -> (ritardo canale + wow/flutter) -> preamp MIC/LINE -> EQ -> saturazione a nastro
//         (pre-enfasi, isteresi magnetica, 4x oversampling) con autogain -> risposta della macchina (fit del grafico)
//         -> compressore VCA 6:1 in parallelo (COMP) -> fader.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace tape {
inline constexpr double PI = 3.14159265358979323846;

struct Params {
    double input = 0;                 // dB, 0..+10 (con autogain)
    double high = 0, midGain = 0, midFreq = 1000, lowGain = 0, lowFreq = 100;
    double faderDb = 0;
    int channel = 0;                  // 0..7
    bool mic = false;                 // false = LINE, true = MIC
    double comp = 0;                  // 0..1: dry/wet del compressore VCA
    double hiss = .5;                 // 0 = OFF, 0.5 = fruscio normale del canale, 1 = +12 dB
    double calDb = 0;                 // livello operativo del nastro: 185 = 0, 250 = +2.6, 320 = +4.8, 500 = +8.6 dB di spinta

    bool same(const Params& o) const {
        return input == o.input && high == o.high && midGain == o.midGain && midFreq == o.midFreq && lowGain == o.lowGain &&
               lowFreq == o.lowFreq && faderDb == o.faderDb && channel == o.channel && mic == o.mic && comp == o.comp && calDb == o.calDb && hiss == o.hiss;
    }
};

enum class FT { PK, LP, HP, HS, LS };

inline std::array<double, 5> coef(FT t, double f, double Q, double G, double sr) {
    const double A = std::pow(10.0, G / 40), w = 2 * PI * std::min(f, sr * 0.45) / sr, s = std::sin(w), c = std::cos(w),
                 al = s / (2 * Q);
    double b0, b1, b2, a0, a1, a2;
    if (t == FT::PK) { b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }
    else if (t == FT::LP) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }
    else if (t == FT::HP) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }
    else {
        const double sq = 2 * std::sqrt(A) * al;
        if (t == FT::HS) {
            b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
            a0 = (A + 1) - (A - 1) * c + sq; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sq;
        } else {
            b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
            a0 = (A + 1) + (A - 1) * c + sq; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sq;
        }
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

struct BQ {
    double c[5] = { 1, 0, 0, 0, 0 }, s[4] = { 0, 0, 0, 0 };
    void set(const std::array<double, 5>& n) { for (int i = 0; i < 5; ++i) c[i] = n[i]; }
    void reset() { for (double& v : s) v = 0; }
    double p(double x) {
        const double y = c[0] * x + c[1] * s[0] + c[2] * s[1] - c[3] * s[2] - c[4] * s[3];
        s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y;
        return y;
    }
};

// Mezza banda (halfband) FIR a fase lineare: meta' dei coefficienti sono zero e il centro vale 0.5.
// h[j] = coefficiente all'offset dispari 2j+1 dal centro, c = 2K+1.
template <int K> struct HalfBand {
    double h[K + 1];
    HalfBand() {
        const int c = 2 * K + 1; const double beta = 7.0;
        auto bessel0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        double sum = 0;
        for (int j = 0; j <= K; ++j) {
            const double o = 2 * j + 1, r = o / c;
            h[j] = ((j & 1) ? -1.0 : 1.0) / (PI * o) * bessel0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / bessel0(beta);
            sum += h[j];
        }
        for (int j = 0; j <= K; ++j) h[j] *= 0.25 / sum;      // 0.5 (centro) + 2*sum(h) = 1  -> guadagno unitario in continua
    }
};
template <int K, int RING> struct HBUp {          // 1 campione -> 2 campioni (fs -> 2fs)
    const HalfBand<K>* hb = nullptr; double buf[RING] = {}; int pos = 0;
    void reset() { for (double& v : buf) v = 0; pos = 0; }
    void push(double x, double& y0, double& y1) {
        constexpr int M = RING - 1;
        pos = (pos + 1) & M; buf[pos] = x;
        double s = 0;
        for (int j = 0; j <= K; ++j) s += hb->h[j] * (buf[(pos - K - 1 - j) & M] + buf[(pos - K + j) & M]);
        y0 = 2 * s; y1 = buf[(pos - K) & M];
    }
};
template <int K, int RING> struct HBDown {        // 2 campioni -> 1 campione (2fs -> fs)
    const HalfBand<K>* hb = nullptr; double buf[RING] = {}; int pos = 0;
    void reset() { for (double& v : buf) v = 0; pos = 0; }
    double push(double v0, double v1) {
        constexpr int M = RING - 1, c = 2 * K + 1;
        pos = (pos + 1) & M; buf[pos] = v0; pos = (pos + 1) & M; buf[pos] = v1;
        double s = 0;
        for (int j = 0; j <= K; ++j) { const int o = 2 * j + 1; s += hb->h[j] * (buf[(pos - c - o) & M] + buf[(pos - c + o) & M]); }
        return 0.5 * buf[(pos - c) & M] + s;
    }
};

// Oversampling 4x a due stadi di mezza banda (48k->96k->192k): ~4 volte piu' leggero di un FIR unico a 97 tappe,
// con circa 70 dB di reiezione. Latenza totale: 27 + 4.5 = 31.5 campioni (arrotondata a 32).
struct OS {
    static constexpr int LAT = 32;
    HalfBand<13> hb1; HalfBand<4> hb2;
    HBUp<13, 32> up1; HBUp<4, 16> up2; HBDown<4, 32> dn2; HBDown<13, 64> dn1;
    OS() { up1.hb = &hb1; dn1.hb = &hb1; up2.hb = &hb2; dn2.hb = &hb2; }
    OS(const OS&) = delete; OS& operator=(const OS&) = delete;
    void reset() { up1.reset(); up2.reset(); dn2.reset(); dn1.reset(); }
    template <class F> double run(double x, F&& fn) {
        double a[2], d[2];
        up1.push(x, a[0], a[1]);
        for (int i = 0; i < 2; ++i) {
            double b0, b1;
            up2.push(a[i], b0, b1);
            d[i] = dn2.push(fn(b0), fn(b1));
        }
        return dn1.push(d[0], d[1]);
    }
};

// tanh razionale (Pade 7/6), errore < 1e-4 fino a |x| = 4.9, poi satura
inline double fastTanh(double x) {
    if (x > 4.9) return 1.0;
    if (x < -4.9) return -1.0;
    const double t = x * x;
    return x * (135135.0 + t * (17325.0 + t * (378.0 + t))) / (135135.0 + t * (62370.0 + t * (3150.0 + 28.0 * t)));
}

// Isteresi magnetica (Jiles-Atherton semplificata)
struct JA {
    double M = 0, Hp = 0, bias = 0.025;
    void reset() { M = Hp = 0; }
    double operator()(double v) {
        const double H = v * 0.5 + bias, dH = H - Hp;
        if (dH == 0) return M;
        const double dl = dH > 0 ? 1 : -1, a = 0.4, al = 0.002, k = 0.3, c = 0.85, t = fastTanh((H + al * M) / a), ir = t - M;
        double dm = 0;
        if (ir * dl > 0) dm = ir / (k * dl - al * ir);
        M += ((1 - c) * dm + c * (1 - t * t) / a) * dH;
        M = std::max(-1.2, std::min(1.2, M));
        Hp = H;
        return M;
    }
};

// Compressore VCA, ratio 6:1, rivelatore RMS, soft knee, attacco lento / rilascio veloce, makeup automatico.
struct VcaComp {
    static constexpr double RATIO = 6.0, THRESH = -24.0, KNEE = 8.0, ATTACK_S = 0.035, RELEASE_S = 0.050, RMS_S = 0.010;
    double ms = 0, gr = 0, kRms = 0, atk = 0, rel = 0, atk8 = 0, rel8 = 0, makeup = 0;
    void prepare(double sr) {
        kRms = 1 - std::exp(-1 / (sr * RMS_S)); atk = std::exp(-1 / (sr * ATTACK_S)); rel = std::exp(-1 / (sr * RELEASE_S));
        atk8 = std::pow(atk, 8); rel8 = std::pow(rel, 8);
        makeup = -THRESH * (1 - 1 / RATIO) * 0.25;
        ms = 0; gr = 0;
    }
    void track(double x) { ms += kRms * (x * x - ms); }       // rivelatore RMS: a ogni campione
    // calcolo del guadagno (log/pow) ogni 8 campioni nel Core, con i coefficienti elevati all'8
    double step() { return compute(atk8, rel8); }
    // versione campione per campione (usata nei test)
    double gain(double x) { track(x); return compute(atk, rel); }
    double compute(double atkC, double relC) {
        const double lvl = 10 * std::log10(ms + 1e-12), over = lvl - THRESH;
        double outL;
        if (2 * over < -KNEE) outL = lvl;
        else if (2 * std::abs(over) <= KNEE) { const double q = over + KNEE / 2; outL = lvl + (1 / RATIO - 1) * q * q / (2 * KNEE); }
        else outL = THRESH + over / RATIO;
        const double target = outL - lvl;                       // <= 0 dB
        const double k = target < gr ? atkC : relC;             // piu' riduzione = attacco (lento)
        gr = k * gr + (1 - k) * target;
        return std::pow(10.0, (gr + makeup) / 20);
    }
};

// Ballistica VU: due poli (circa 300 ms al 99%), 0 VU = -18 dBFS RMS
struct VuBall {
    double a = 0, b = 0, k = 0;
    void prepare(double sr) { k = 1 - std::exp(-1 / (sr * 0.045)); a = b = 0; }
    void p(double v) { a += k * (v - a); b += k * (a - b); }
    double db(double ref = 0.126) const { return std::max(-22.0, std::min(4.0, 20 * std::log10(b * 1.11 / ref + 1e-6))); }
};

// Differenze fra gli 8 canali (circuiteria). Derivate dallo spread delle curve del grafico (pochi decimi di dB al picco
// di 55 Hz, fino a ~1 dB oltre i 14 kHz); i canali esterni (1 e 8) sono piu' scuri e consumati.
struct ChannelTable {
    static constexpr double hump[8] = { .30, -.15, .25, -.30, .15, -.20, .05, .35 };    // dB sul picco 55 Hz
    static constexpr double dip[8] = { -.45, .05, -.10, .10, 0, -.05, .10, -.40 };       // dB extra sulla buca 100 Hz
    static constexpr double hfpk[8] = { -1.2, -.2, .5, -.3, -.6, 0, -.3, -1.0 };         // dB sul rialzo 14 kHz
    static constexpr double lpScale[8] = { .965, .995, 1.0, .99, .98, 1.0, .995, .96 };   // taglio alto
    static constexpr double trim[8] = { .2, -.1, .15, -.2, .1, 0, -.15, .25 };            // dB
    static constexpr double dly[8] = { 0, 9, 17, 26, 34, 43, 51, 60 };                    // microsecondi
    static constexpr double noise[8] = { -58, -62, -63, -63, -62, -63, -62, -57 };        // dB
    static constexpr double drv[8] = { 1.06, .98, 1, 1.02, .97, 1, 1.01, 1.05 };
};

// pomello HISS: 0 = OFF, da 0 a 0.5 sale da -30 dB al fruscio normale (0 dB), da 0.5 a 1 sale fino a +12 dB
inline double hissDb(double v) { return v < .5 ? -30.0 * (.5 - v) / .5 : 24.0 * (v - .5); }
inline double hissGain(double v) { return v <= .005 ? 0.0 : std::pow(10.0, hissDb(v) / 20); }

class Core {
public:
    double vuIn = -20, vuOut = -20, over = 0, grDb = 0;

    static constexpr int BUF = 16384;

    void prepare(double sampleRate, uint32_t seed = 1) {
        sr = sampleRate;
        for (auto& b : tapeF) b.reset();
        for (auto& b : eqF) b.reset();
        for (auto& b : micF) b.reset();
        pre.reset(); de.reset(); os.reset(); ja.reset();
        std::fill(std::begin(dl), std::end(dl), 0.0);
        wp = 0; wf = wfStep = 0; gCur = 1; gStep = 0; blk = 0; grDb = 0; dcx = 0; rin = rout = 1e-4; ag = 1; tt = 0; sd = 2.0; fsm = 1; mixS = 0; xp = 0; wprev = 0; pk = 0;
        comp.prepare(sr); vSat.prepare(sr); trimS = trimT = 1; grVis = 0; kGr = 1 - std::exp(-1 / (sr * .08));
        rng = 0x9E3779B9u * (seed + 1);
        baseDelay = std::round(6.0 * sr / 48000.0);
        pkDecay = std::exp(-1 / (sr * 0.25));
        haveParams = false;
        update();
    }
    int latencySamples() const { return (int) baseDelay + OS::LAT; }
    void setParams(const Params& p) {
        if (haveParams && P.same(p)) return;
        P = p; haveParams = true; update();
    }

    float process(float in) {
        // wow e flutter (ampiezze in secondi, quindi indipendenti dalla frequenza di campionamento); LFO lenti:
        // si calcolano ogni 8 campioni e si interpolano linearmente
        if ((blk & 7) == 0) {
            tt += 8.0 / sr;
            wfStep = ((2.6 * std::sin(6.283185 * .8 * tt) + .15 * std::sin(6.283185 * 9 * tt) + .5 * std::sin(6.283185 * .23 * tt)) / 48000.0 * sr - wf) / 8.0;
        }
        wf += wfStep;
        dl[wp] = in * tg;
        const double pos = wp - (baseDelay + dsamp + wf);
        const int i0 = (int) std::floor(pos); const double t = pos - i0;
        const double xm1 = dl[(i0 - 1) & (BUF - 1)], x0_ = dl[i0 & (BUF - 1)], x1 = dl[(i0 + 1) & (BUF - 1)], x2 = dl[(i0 + 2) & (BUF - 1)];
        const double c1 = .5 * (x1 - xm1), c2 = xm1 - 2.5 * x0_ + 2 * x1 - .5 * x2, c3 = .5 * (x2 - xm1) + 1.5 * (x0_ - x1);
        double x = ((c3 * t + c2) * t + c1) * t + x0_;
        wp = (wp + 1) & (BUF - 1);
        // preamp MIC (trasformatore; neutro in LINE) e EQ di canale (prima del nastro, come sulla console)
        if (micOn) x = micF[1].p(micF[0].p(x));
        for (int i = 0; i < 3; ++i) if (eqOn[i]) x = eqF[i].p(x);
        sd += (drive - sd) * .0004; trimS += (trimT - trimS) * .0004; fsm += (fad - fsm) * .003; mixS += (P.comp - mixS) * .002;
        // saturazione: pre-enfasi -> isteresi (4x oversampling) -> de-enfasi
        const double z = pre.p(x * sd);
        const double y = de.p(os.run(z, ja) / (sd * 1.05));
        // autogain (non reagisce al silenzio: niente effetto gate)
        dcx += 0.0005 * (y - dcx);                          // la polarizzazione genera una continua: non va misurata
        const double yd = y - dcx;
        rin = .9996 * rin + .0004 * x * x; rout = .9996 * rout + .0004 * yd * yd;
        if (rin > 2e-5) ag += (std::min(4.0, std::max(.25, std::sqrt(rin / (rout + 1e-12)) * trimS)) - ag) * .0006;
        double o = (y - dcx) * ag;
        // risposta in frequenza della macchina (fit del grafico), poi fruscio e perdita fra tracce
        for (auto& b : tapeF) o = b.p(o);
        const double w = noiseRnd();
        o += (w - .6 * wprev) * nz + xtk * (x - xp) * 4; wprev = w; xp = x;
        // compressore VCA 6:1 in parallelo
        comp.track(o);
        if ((blk & 7) == 7) {                               // log/pow solo ogni 8 campioni; a COMP = 0 il compressore non lavora
            double gNew = 1.0, grT = 0.0;
            if (P.comp > 0.0 || mixS > 1e-3) { gNew = comp.step(); grT = 20 * std::log10(gNew) - comp.makeup; }
            else comp.gr *= .98;
            grDb += (grT - grDb) * .008;                    // <= 0
            gStep = (gNew - gCur) / 8.0;
        }
        gCur += gStep;
        grVis += kGr * (-grDb * mixS - grVis);
        o = (o * (1 - mixS) + o * gCur * mixS) * fsm;
        ++blk;
        // meter
        pk = std::max(pk * pkDecay, std::abs(x * sd));
        vSat.p(std::abs(z));
        return (float) o;
    }

    // Meter sinistro: saturazione (livello all'ingresso del nastro, 0 VU = inizio saturazione forte; rosso se troppa).
    // Meter destro: riduzione di guadagno del compressore (a riposo = -20, fondo scala = 12 dB di riduzione).
    void updateMeters() {
        vuIn = vSat.db(0.56);
        const double ramp = std::max(0.0, std::min(1.0, (vuIn + 1.0) / 4.0)), peakRamp = std::max(0.0, std::min(1.0, (pk - 1.6) / 1.5));
        over = std::max(ramp, peakRamp);
        const double p = std::max(0.0, std::min(1.0, grVis / 12.0));
        vuOut = 20 * std::log10(std::max(.1, p * 1.4125));
    }

private:
    double sr = 48000;
    Params P; bool haveParams = false;
    BQ tapeF[11], eqF[3], micF[2], pre, de;
    OS os; JA ja; VcaComp comp; VuBall vSat;
    double dl[BUF] = {};
    int wp = 0;
    double wf = 0, wfStep = 0, gCur = 1, gStep = 0; unsigned blk = 0; bool micOn = false, eqOn[3] = { false, false, false };
    double trimS = 1, trimT = 1, grVis = 0, kGr = .0003, dcx = 0, rin = 1e-4, rout = 1e-4, ag = 1, tt = 0, sd = 2.0, fsm = 1, mixS = 0, xp = 0, wprev = 0, pk = 0, pkDecay = .9997;
    double gin = 1, drive = 2.0, tg = 1, fad = 1, nz = 0, dsamp = 0, xtk = 0, baseDelay = 6;
    uint32_t rng = 0x12345678u;
    double noiseRnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng / 4294967295.0) * 2 - 1; }

    void update() {
        const int ch = std::clamp(P.channel, 0, 7);
        // curva della macchina: fit su 160 punti del grafico (errore medio 0.19 dB)
        tapeF[0].set(coef(FT::HP, 31.05, .945, 0, sr));
        tapeF[1].set(coef(FT::PK, 55.9, 1.81, 2.20 + ChannelTable::hump[ch], sr));
        tapeF[2].set(coef(FT::PK, 102.0, 1.98, -3.19 + ChannelTable::dip[ch], sr));
        tapeF[3].set(coef(FT::PK, 208.2, 6.0, 1.05, sr));
        tapeF[4].set(coef(FT::PK, 4267.7, .745, -.74, sr));
        tapeF[5].set(coef(FT::PK, 14393.5, 1.08, 6.10 + ChannelTable::hfpk[ch], sr));
        tapeF[6].set(coef(FT::PK, 90.0, .3, .586, sr));
        static const double BUT[4] = { .5098, .6013, .9000, 2.5629 };      // Butterworth di 8o ordine
        const double lpf = 18500.0 * ChannelTable::lpScale[ch];
        for (int i = 0; i < 4; ++i) tapeF[7 + i].set(coef(FT::LP, lpf, BUT[i] * .8, 0, sr));
        pre.set(coef(FT::HS, 4500, .7, 6, sr));
        de.set(coef(FT::HS, 4500, .7, -6, sr));
        // stadi inattivi (guadagno ~0) vengono saltati per risparmiare CPU; il loro stato si azzera
        const bool on[3] = { std::abs(P.high) > .005, std::abs(P.midGain) > .005, std::abs(P.lowGain) > .005 };
        for (int i = 0; i < 3; ++i) { if (!on[i]) eqF[i].reset(); eqOn[i] = on[i]; }
        eqF[0].set(coef(FT::HS, 6000, .6, P.high, sr));
        eqF[1].set(coef(FT::PK, P.midFreq, .8, P.midGain, sr));
        eqF[2].set(coef(FT::LS, P.lowFreq, .7, P.lowGain, sr));
        if (!P.mic) { micF[0].reset(); micF[1].reset(); }
        micOn = P.mic;
        micF[0].set(coef(FT::LS, 90, .7, 1.5, sr));
        micF[1].set(coef(FT::HS, 11000, .7, -1.5, sr));
        ja.bias = P.mic ? 0.06 : 0.025;
        gin = std::pow(10.0, (P.input + P.calDb) / 20);   // INPUT + livello operativo del nastro
        drive = 2.0 * gin * ChannelTable::drv[ch] * (P.mic ? 1.6 : 1.0);
        tg = std::pow(10.0, ChannelTable::trim[ch] / 20);
        fad = P.faderDb <= -79.9 ? 0.0 : std::pow(10.0, P.faderDb / 20);   // fondo corsa = -inf (muto vero)
        nz = std::pow(10.0, ChannelTable::noise[ch] / 20) * .08 * (P.mic ? 1.5 : 1.0) * hissGain(P.hiss);
        trimT = std::pow(10.0, -0.15 * std::max(0.0, P.input + P.calDb) / 20);        // la saturazione densa suona piu' forte a pari RMS: leggera compensazione
        dsamp = ChannelTable::dly[ch] * 1e-6 * sr;
        xtk = .0025 * ((ch == 0 || ch == 7) ? 1.6 : 1.0);
    }
};
} // namespace tape
