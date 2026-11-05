#pragma once
// Display alfanumerico a 14 segmenti stile anni '80 (VFD/LED), disegnato a vettori: nessun file di font necessario.
// Cella: larghezza 1, altezza 2 (unita' = meta' altezza carattere). Stessa geometria di Tests/preview.py.
#include <juce_graphics/juce_graphics.h>

namespace seg {
// ordine dei bit: a b c d e f g1 g2 h i j k l m
inline constexpr float G = .12f;
inline const float SEGS[14][4] = {
    { G, 0, 1 - G, 0 }, { 1, G, 1, 1 - G }, { 1, 1 + G, 1, 2 - G }, { G, 2, 1 - G, 2 }, { 0, 1 + G, 0, 2 - G }, { 0, G, 0, 1 - G },
    { G, 1, .5f - G, 1 }, { .5f + G, 1, 1 - G, 1 },
    { G * 1.6f, G * 2, .5f - G * 1.2f, 1 - G * 1.6f }, { .5f, G, .5f, 1 - G }, { 1 - G * 1.6f, G * 2, .5f + G * 1.2f, 1 - G * 1.6f },
    { .5f - G * 1.2f, 1 + G * 1.6f, G * 1.6f, 2 - G * 2 }, { .5f, 1 + G, .5f, 2 - G }, { .5f + G * 1.2f, 1 + G * 1.6f, 1 - G * 1.6f, 2 - G * 2 } };
enum : unsigned { a = 1, b = 2, c = 4, d = 8, e = 16, f = 32, g1 = 64, g2 = 128, h = 256, i = 512, j = 1024, k = 2048, l = 4096, m = 8192 };

inline unsigned mask(juce::juce_wchar ch) {
    switch (juce::CharacterFunctions::toUpperCase(ch)) {
        case '0': return a | b | c | d | e | f | j | k;   case '1': return b | c | j;
        case '2': return a | b | g1 | g2 | e | d;          case '3': return a | b | c | d | g2;
        case '4': return f | g1 | g2 | b | c;              case '5': return a | f | g1 | g2 | c | d;
        case '6': return a | f | e | d | c | g1 | g2;      case '7': return a | b | c;
        case '8': return a | b | c | d | e | f | g1 | g2;  case '9': return a | b | c | d | f | g1 | g2;
        case 'A': return a | b | c | e | f | g1 | g2;      case 'B': return a | b | c | d | i | l | g2;
        case 'C': return a | d | e | f;                    case 'D': return a | b | c | d | i | l;
        case 'E': return a | d | e | f | g1;               case 'F': return a | e | f | g1;
        case 'G': return a | c | d | e | f | g2;           case 'H': return b | c | e | f | g1 | g2;
        case 'I': return a | d | i | l;                    case 'K': return e | f | g1 | j | m;
        case 'L': return d | e | f;                        case 'M': return b | c | e | f | h | j;
        case 'N': return b | c | e | f | h | m;            case 'O': return a | b | c | d | e | f;
        case 'P': return a | b | e | f | g1 | g2;          case 'Q': return a | b | c | d | e | f | m;
        case 'R': return a | b | e | f | g1 | g2 | m;      case 'S': return a | f | g1 | g2 | c | d;
        case 'T': return a | i | l;                        case 'U': return b | c | d | e | f;
        case 'J': return b | c | d | e;                    case 'V': return e | f | k | j;
        case 'W': return b | c | e | f | k | m;            case 'Y': return h | j | l;
        case 'X': return h | j | k | m;                    case 'Z': return a | d | j | k;
        case '+': return g1 | g2 | i | l;                  case '-': return g1 | g2;
        case '%': return f | c | j | k | g1 | g2;          default: return 0;
    }
}

struct Style { float charH, slant = .14f, thick = .11f, cellW = .56f, gap = .30f; };
inline float advance(const Style& s) { return s.charH * (s.cellW + s.gap); }
inline float width(const juce::String& t, const Style& s) {
    int n = 0; for (int q = 0; q < t.length(); ++q) if (t[q] != '.') ++n;
    return n > 0 ? n * advance(s) - s.charH * s.gap : 0.f;
}
// testo centrato in area; ghost = trasparenza dei segmenti spenti
inline void draw(juce::Graphics& g, const juce::String& t, juce::Rectangle<float> area, const Style& s, juce::Colour on, float ghost) {
    const float u = s.charH * .5f, w = width(t, s), x0 = area.getCentreX() - w * .5f - u * s.slant, y0 = area.getCentreY() - u;
    const float th = s.charH * s.thick;
    auto P = [&](float cx, float px, float py) { return juce::Point<float>(cx + px * u * s.cellW * 2.f + (2.f - py) * u * s.slant, y0 + py * u); };
    const juce::PathStrokeType stroke(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), glow(th * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    float cx = x0;
    for (int n = 0; n < t.length(); ++n) {
        const auto ch = t[n];
        if (ch == '.') continue;
        const unsigned mk = mask(ch);
        const bool dp = n + 1 < t.length() && t[n + 1] == '.';
        juce::Path lit, off;
        for (int k = 0; k < 14; ++k) {
            const float* q = SEGS[k];
            auto& p = (mk >> k & 1u) ? lit : off;
            p.startNewSubPath(P(cx, q[0], q[1])); p.lineTo(P(cx, q[2], q[3]));
        }
        g.setColour(on.withAlpha(on.getFloatAlpha() * ghost)); g.strokePath(off, stroke);
        g.setColour(on.withAlpha(on.getFloatAlpha() * .16f)); g.strokePath(lit, glow);
        g.setColour(on); g.strokePath(lit, stroke);
        const auto dpp = P(cx, 1.f + s.gap * .5f / s.cellW, 2.f);
        g.setColour(dp ? on : on.withAlpha(on.getFloatAlpha() * ghost));
        g.fillEllipse(dpp.x - th * .7f, dpp.y - th * .7f, th * 1.4f, th * 1.4f);
        cx += advance(s);
    }
}
} // namespace seg
