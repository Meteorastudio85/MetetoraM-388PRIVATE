#include "../Source/TapeCore.h"
#include <cstdio>
#include <cstdlib>
// Test 1: armoniche e livello al variare dell'input gain. Test 2: niente effetto gate dopo il silenzio.
int main() {
    int fails = 0;
    for (double g : { 0.0, 2.5, 5.0, 10.0 }) {
        tape::Core c; tape::Params p; p.input = g; c.prepare(48000); c.setParams(p);
        const int n = 48000 * 7; double rms = 0, re[3] = {0,0,0}, im[3] = {0,0,0}; int cnt = 0; bool fin = true;
        const double f[3] = { 220, 440, 660 };
        for (int i = 0; i < n; ++i) {
            const float x = (float)(.25 * std::sin(2 * tape::PI * 220 * i / 48000.0) + .1 * (rand() / (double)RAND_MAX - .5));
            const float y = c.process(x);
            if (!std::isfinite(y)) fin = false;
            if (i > n - 16384) { rms += y * y; ++cnt; for (int h = 0; h < 3; ++h) { const double w = 2 * tape::PI * f[h] * i / 48000.0; re[h] += y * std::cos(w); im[h] += y * std::sin(w); } }
        }
        c.updateMeters();
        const double m1 = std::hypot(re[0], im[0]);
        std::printf("gain %+5.1f dB  rms %.4f  H2 %.1f dB  H3 %.1f dB  vuIn %.1f  over %.2f  %s\n", g, std::sqrt(rms / cnt),
                    20 * std::log10(std::hypot(re[1], im[1]) / m1), 20 * std::log10(std::hypot(re[2], im[2]) / m1), c.vuIn, c.over, fin ? "ok" : "NaN!");
        if (!fin) ++fails;
    }
    // gate: tono costante 1 s, silenzio 1 s, poi di nuovo il tono: l'attacco non deve essere attenuato
    tape::Core c; tape::Params p; c.prepare(48000); c.setParams(p);
    auto tone = [&](int i) { return (float)(.1 * std::sin(2 * tape::PI * 220 * i / 48000.0)); };
    double steady = 0, burst = 0; int a = 0, b = 0;
    for (int i = 0; i < 48000 * 3; ++i) {
        const bool silent = i >= 48000 && i < 96000;
        const float y = c.process(silent ? 0.f : tone(i));
        if (i >= 40000 && i < 40000 + 2400) { steady += y * y; ++a; }
        if (i >= 96000 && i < 96000 + 2400) { burst += y * y; ++b; }
    }
    const double ratio = std::sqrt(burst / b) / std::sqrt(steady / a);
    std::printf("attacco dopo silenzio / regime: %.2f (1.00 = nessun gate)\n", ratio);
    if (ratio < .8 || ratio > 1.25) ++fails;
    // Test 3: l'EQ della console deve agire davvero
    auto gainDb = [](double f, tape::Params q) {
        tape::Core cc; cc.prepare(48000); cc.setParams(q); double ia = 0, ib = 0; const int n = 48000 * 3;
        for (int i = 0; i < n; ++i) { const float x = (float)(.05 * std::sin(2 * tape::PI * f * i / 48000.0)); const float y = cc.process(x); if (i > n - 9600) { ia += x * x; ib += y * y; } }
        return 10 * std::log10(ib / ia); };
    tape::Params o, q; const double b10k = gainDb(10000, o), b1k = gainDb(1000, o), b100 = gainDb(100, o);
    q = o; q.high = 15;                      const double eh = gainDb(10000, q) - b10k;
    q = o; q.midGain = 15; q.midFreq = 1000; const double em = gainDb(1000, q) - b1k;
    q = o; q.lowGain = 15; q.lowFreq = 200;  const double el = gainDb(100, q) - b100;
    std::printf("EQ: high +15 -> %+.1f dB @10k | mid +15 -> %+.1f dB @1k | low +15 -> %+.1f dB @100\n", eh, em, el);
    if (eh < 8 || em < 13 || el < 11) ++fails;
    // Test 4: MIC e LINE devono suonare diversi (piu' armoniche pari in MIC) a parita' di livello
    auto harm = [](bool mic, double& rmsOut, double& h2) {
        tape::Core cc; tape::Params q; q.mic = mic; q.input = 0; cc.prepare(48000); cc.setParams(q);
        const int n = 48000 * 7; double r = 0, re1 = 0, im1 = 0, re2 = 0, im2 = 0; int cnt = 0;
        for (int i = 0; i < n; ++i) { const float y = cc.process((float)(.25 * std::sin(2 * tape::PI * 220 * i / 48000.0)));
            if (i > n - 16384) { r += y * y; ++cnt; const double w = 2 * tape::PI * 220 * i / 48000.0; re1 += y * std::cos(w); im1 += y * std::sin(w); re2 += y * std::cos(2 * w); im2 += y * std::sin(2 * w); } }
        rmsOut = std::sqrt(r / cnt); h2 = 20 * std::log10(std::hypot(re2, im2) / std::hypot(re1, im1)); };
    double rl, hl, rm, hm; harm(false, rl, hl); harm(true, rm, hm);
    std::printf("LINE: rms %.3f H2 %.1f dB | MIC: rms %.3f H2 %.1f dB\n", rl, hl, rm, hm);
    if (hm < hl + 3 || std::abs(20 * std::log10(rm / rl)) > 3) ++fails;
    // Test 5: risposta in frequenza vs grafico (segnale piccolo, canale 3): 55Hz ~ +3, 100Hz ~ -3, 16k >= -3 dB, 17k ~ -3 dB
    {
        tape::Params q; q.channel = 2; q.input = 0;
        const double fs[] = { 31, 55, 100, 200, 1000, 4000, 10000, 14000, 16000, 17000, 20000 };
        double ref = 0, vals[11];
        for (int k = 0; k < 11; ++k) vals[k] = gainDb(fs[k], q);
        ref = vals[4];
        for (int k = 0; k < 11; ++k) std::printf("  FR %6.0f Hz: %+5.2f dB (rel 1k)\n", fs[k], vals[k] - ref);
        if (vals[1] - ref < 1.5 || vals[1] - ref > 4.5 || vals[2] - ref > -1.5 || vals[2] - ref < -4.5 || vals[8] - ref < -4 || vals[8] - ref > 3 || vals[10] - ref > -3) ++fails;
    }
    // Test 6: compressore VCA 6:1 - riduzione di guadagno su segnale forte, dry/wet, nessun rumore su segnale basso
    {
        auto lvl = [](double amp, double comp) { tape::Core cc; tape::Params q; q.comp = comp; cc.prepare(48000); cc.setParams(q);
            double r = 0; int cnt = 0; const int n = 48000 * 3;
            for (int i = 0; i < n; ++i) { const float y = cc.process((float)(amp * std::sin(2 * tape::PI * 440 * i / 48000.0))); if (i > n - 9600) { r += y * y; ++cnt; } }
            return 10 * std::log10(r / cnt); };
        const double d1 = lvl(.5, 0) - lvl(.05, 0), c1 = lvl(.5, 1) - lvl(.05, 1);
        std::printf("COMP: escursione 20 dB in ingresso -> dry %.1f dB, wet %.1f dB (atteso ~ 20 e <~ 8)\n", d1, c1);
        if (c1 > d1 - 6 || lvl(.5, .5) > lvl(.5, 0) + 3 || lvl(.5, .5) < lvl(.5, 1) - 3) ++fails;
        // attacco lento: un burst di 10 ms deve passare quasi intatto, il rilascio deve essere piu' rapido dell'attacco
        tape::VcaComp v; v.prepare(48000); double g10 = 0, g200 = 0, gg = 1;
        for (int i = 0; i < 48000; ++i) { v.track(.5 * std::sin(2 * tape::PI * 440 * i / 48000.0)); if (i % 8 == 7) gg = v.step(); if (i == 480) g10 = gg; if (i == 9600) g200 = gg; }
        std::printf("COMP: guadagno dopo 10 ms %.2f, dopo 200 ms %.2f (attacco lento)\n", g10, g200);
        if (g10 < g200 * 1.15) ++fails;
    }
    // Test 7: girando INPUT GAIN a suono in corso il volume non deve schizzare (musica sintetica con colpi e basso)
    {
        tape::Core c; tape::Params q; c.prepare(48000); c.setParams(q);
        unsigned r = 1; auto rnd = [&]() { r = r * 1664525u + 1013904223u; return (r >> 8) / 8388608.0 - 1.0; };
        double lp = 0, blk = 0, base = -99, worst = -99; int cnt = 0, nb = 0;
        for (int i = 0; i < 48000 * 6; ++i) {
            const double t = i / 48000.0;
            if (i == 48000 * 3) { q.input = 10; c.setParams(q); }
            lp += .2 * (rnd() - lp);
            const float x = (float)(.12 * (.6 * lp + .8 * std::sin(2 * tape::PI * 55 * t) * (.5 + .5 * std::sin(2 * tape::PI * 2 * t)) + .7 * std::exp(-std::fmod(t, .5) * 18) * rnd()));
            const float y = c.process(x); blk += y * y; ++cnt;
            if (cnt == 4800) { const double db = 10 * std::log10(blk / cnt + 1e-12); ++nb; if (t > 2 && t < 3) base = std::max(base, db); if (t > 3.05) worst = std::max(worst, db); blk = 0; cnt = 0; }
        }
        std::printf("INPUT 0 -> +10 dB a suono in corso: picco RMS (100 ms) dopo la manovra %+.1f dB rispetto al massimo di prima\n", worst - base);
        if (worst - base > 2.0) ++fails;
    }
    // Test 8: meter - saturazione cresce con il gain; compressione = 0 con COMP a zero, > 0 con COMP al massimo su segnale forte
    {
        auto run = [](double gdb, double comp, double& sat, double& gr, double& over) {
            tape::Core c; tape::Params q; q.input = gdb; q.comp = comp; c.prepare(48000); c.setParams(q);
            for (int i = 0; i < 48000 * 3; ++i) { c.process((float)(.3 * std::sin(2 * tape::PI * 220 * i / 48000.0))); if (i % 480 == 0) c.updateMeters(); }
            sat = c.vuIn; gr = c.vuOut; over = c.over; };
        double s0, g0, o0, s1, g1, o1, s2, g2, o2;
        run(0, 0, s0, g0, o0); run(10, 0, s1, g1, o1); run(10, 1, s2, g2, o2);
        std::printf("METER: sat gain0 %.1f VU (over %.2f) | gain10 %.1f VU (over %.2f) | GR comp0 %.1f, comp1 %.1f\n", s0, o0, s1, o1, g1, g2);
        if (!(s1 > s0 + 4) || o0 > .1 || o1 < .9 || g1 > -19.5 || g2 < -17) ++fails;
    }
    // Test 9: fader a fondo corsa (-80) = muto vero; stabilita' a tutte le frequenze di campionamento con impostazioni estreme
    {
        tape::Core c; tape::Params q; q.faderDb = -80; c.prepare(48000); c.setParams(q); double m = 0;
        for (int i = 0; i < 48000 * 2; ++i) { const float y = c.process((float)(.5 * std::sin(2 * tape::PI * 440 * i / 48000.0))); if (i > 48000) m = std::max(m, (double) std::abs(y)); }
        std::printf("FADER -inf: picco in uscita %.2g (atteso 0)\n", m);
        if (m > 1e-9) ++fails;
        bool ok = true;
        for (double sr : { 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 }) for (int ch = 0; ch < 8; ++ch) {
            tape::Core cc; tape::Params e; e.channel = ch; e.input = 10; e.mic = true; e.comp = 1; e.high = 15; e.midGain = 15; e.lowGain = 15; e.faderDb = 10;
            cc.prepare(sr); cc.setParams(e);
            for (int i = 0; i < (int) sr; ++i) { const float y = cc.process((float)(1.0 * std::sin(2 * tape::PI * 60 * i / sr))); if (!std::isfinite(y) || std::abs(y) > 20) ok = false; }
        }
        std::printf("STABILITA' 22k..192k, 8 canali, impostazioni estreme: %s\n", ok ? "ok" : "ERRORE");
        if (!ok) ++fails;
    }
    // Test 10: TAPE LEVEL 500 nWb/m satura molto di piu' una traccia bassa (-12 VU) a pari INPUT
    {
        auto thd = [](double cal) {
            const double sr = 48000, f = 1000; tape::Core c; tape::Params q; q.channel = 5; q.input = 10; q.calDb = cal; c.prepare(sr); c.setParams(q);
            const int n = (int) (sr * 3); double re[6] = {}, im[6] = {};
            for (int i = 0; i < n; ++i) { const double w = 2 * tape::PI * f * i / sr; const double y = c.process((float) (.0447 * std::sin(w)));
                if (i >= n - 48000) for (int h = 1; h <= 5; ++h) { re[h] += y * std::cos(h * w); im[h] += y * std::sin(h * w); } }
            double s = 0; for (int h = 2; h <= 5; ++h) s += (re[h] * re[h] + im[h] * im[h]) / (re[1] * re[1] + im[1] * im[1]);
            return 100 * std::sqrt(s);
        };
        const double a = thd(0), b = thd(8.6);
        std::printf("TAPE LEVEL: traccia -12 VU, INPUT +10 -> THD 185 nWb/m %.2f%%, 500 nWb/m %.2f%%\n", a, b);
        if (!(b > a * 2.5)) ++fails;
    }
    // Test 11: pomello HISS (fruscio a ingresso muto): OFF = silenzio, 0.5 = come prima, 1 = +12 dB
    {
        auto noiseDb = [](double h) {
            tape::Core c; tape::Params q; q.channel = 0; q.hiss = h; c.prepare(48000); c.setParams(q); double s = 0;
            for (int i = 0; i < 48000 * 2; ++i) { const double y = c.process(0.f); if (i >= 48000) s += y * y; }
            return 10 * std::log10(s / 48000 + 1e-30);
        };
        const double off = noiseDb(0), nom = noiseDb(.5), mx = noiseDb(1);
        std::printf("HISS: OFF %.1f dB, normale %.1f dB, MAX %.1f dB (atteso MAX = normale +12)\n", off, nom, mx);
        if (!(off < -200 && std::abs(mx - nom - 12) < .5)) ++fails;
    }
    std::printf(fails ? "FAIL\n" : "PASS\n");
    return fails;
}
