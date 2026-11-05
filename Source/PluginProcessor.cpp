#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamTable.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

// Registro delle istanze aperte nello stesso processo: ogni nuova istanza prende il primo canale (1-8) non usato dalle altre.
static std::mutex gRegMutex;
static std::vector<M388Processor*> gInstances;

// testo dei valori mostrato nell'host e nell'etichetta durante il trascinamento
static juce::String formatValue(const juce::String& id, float v) {
    if (id == "main_pan") return std::abs(v) < .005f ? juce::String("C") : juce::String(v < 0 ? "L " : "R ") + juce::String(juce::roundToInt(std::abs(v) * 100.f));
    if (id == "main_comp") return juce::String(juce::roundToInt(v * 100.f)) + " %";
    if (id == "main_hiss") {
        if (v <= .005f) return "OFF";
        const float db = (float) tape::hissDb(v);
        return juce::String(db > 0.05f ? "+" : "") + juce::String(std::abs(db) < .05f ? 0.f : db, 1) + " dB";
    }
    if (id.endsWith("_freq")) return v >= 1000.f ? juce::String(v / 1000.f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
    if (id == "main_fader" && v <= -79.9f) return "-inf dB";
    return juce::String(v > 0.005f ? "+" : "") + juce::String(v, 1) + " dB";
}
static float parseValue(const juce::String& id, const juce::String& t) {
    const auto s = t.trim().toUpperCase();
    if (id == "main_pan") {
        if (s.startsWith("C")) return 0.f;
        const float a = s.retainCharacters("0123456789.").getFloatValue() / 100.f;
        return s.startsWith("L") ? -a : a;
    }
    if (id == "main_comp") return s.getFloatValue() / 100.f;
    if (id == "main_hiss") {
        if (s.startsWith("OFF")) return 0.f;
        const float db = juce::jlimit(-30.f, 12.f, s.getFloatValue());
        return db < 0 ? .5f - (-db / 30.f) * .5f : .5f + db / 24.f;
    }
    if (s.contains("INF")) return -80.f;
    return s.getFloatValue() * (id.endsWith("_freq") && s.contains("K") ? 1000.f : 1.f);
}

static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& d : PARAMS) {
        if (d.kind == 2) {
            auto names = juce::StringArray::fromTokens(d.choices, "|", "");
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { d.id, 1 }, d.name, names, (int) d.def));
        } else if (d.kind == 3) {
            layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { d.id, 1 }, d.name, d.def > .5f));
        } else {
            juce::NormalisableRange<float> range(d.mn, d.mx);
            if (d.kind == 1) range.setSkewForCentre(std::sqrt(d.mn * d.mx));
            const juce::String id(d.id);
            auto attr = juce::AudioParameterFloatAttributes()
                            .withStringFromValueFunction([id](float v, int) { return formatValue(id, v); })
                            .withValueFromStringFunction([id](const juce::String& t) { return parseValue(id, t); });
            layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { d.id, 1 }, d.name, range, d.def, attr));
        }
    }
    return layout;
}

M388Processor::M388Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "M388", makeLayout()) {
    std::lock_guard<std::mutex> lock(gRegMutex);
    unsigned used = 0;
    for (auto* other : gInstances) used |= 1u << ((int) other->apvts.getRawParameterValue("channel")->load() & 7);
    int ch = (int) (gInstances.size() % 8);                 // se tutti gli 8 canali sono occupati si riparte dall'1
    for (int i = 0; i < 8; ++i) if (!(used & (1u << i))) { ch = i; break; }
    if (auto* par = apvts.getParameter("channel")) par->setValueNotifyingHost(par->convertTo0to1((float) ch));
    gInstances.push_back(this);
}

M388Processor::~M388Processor() {
    std::lock_guard<std::mutex> lock(gRegMutex);
    gInstances.erase(std::remove(gInstances.begin(), gInstances.end(), this), gInstances.end());
}

// pan = bilanciamento a potenza costante (centro = guadagno 1 su entrambi i lati)
static void panGains(float pan, float& l, float& r) {
    const float a = (pan + 1.f) * juce::MathConstants<float>::pi / 4.f;
    l = std::cos(a) * 1.41421356f; r = std::sin(a) * 1.41421356f;
}

void M388Processor::prepareToPlay(double sampleRate, int) {
    for (int i = 0; i < 2; ++i) core[i].prepare(sampleRate, (uint32_t) (i + 1));
    const int lat = core[0].latencySamples();
    setLatencySamples(lat);
    dryLen = juce::jmax(1, lat); dryPos = 0;
    for (auto& d : dry) d.assign((size_t) dryLen, 0.f);
    bypassMix.reset(sampleRate, 0.02);
    bypassMix.setCurrentAndTargetValue(raw("main_bypass") > .5f ? 1.f : 0.f);
    float g[2]; panGains(raw("main_pan"), g[0], g[1]);
    for (int c = 0; c < 2; ++c) { panG[c].reset(sampleRate, 0.03); panG[c].setCurrentAndTargetValue(g[c]); }
}

bool M388Processor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == l.getMainInputChannelSet();
}

tape::Params M388Processor::readParams() const {
    tape::Params p;
    p.input = raw("main_input"); p.high = raw("main_high");
    p.midGain = raw("main_mid_gain"); p.midFreq = raw("main_mid_freq");
    p.lowGain = raw("main_low_gain"); p.lowFreq = raw("main_low_freq");
    p.faderDb = raw("main_fader"); p.channel = (int) raw("channel");
    p.mic = raw("main_micline") > .5f; p.comp = raw("main_comp"); p.hiss = raw("main_hiss");
    static constexpr double CAL_DB[4] = { 0.0, 2.6, 4.8, 8.6 };    // 185 / 250 / 320 / 500 nWb/m
    p.calDb = CAL_DB[juce::jlimit(0, 3, (int) raw("tape_cal"))];
    return p;
}

void M388Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int nc = juce::jmin(buffer.getNumChannels(), 2), n = buffer.getNumSamples();
    // Il nucleo lavora sempre (anche in bypass): cosi' al rientro non escono residui vecchi dal nastro e il passaggio
    // e' una dissolvenza di 20 ms fra segnale elaborato e segnale asciutto ritardato della stessa latenza.
    const bool byp = raw("main_bypass") > .5f;
    bypassMix.setTargetValue(byp ? 1.f : 0.f);
    float tg[2]; panGains(raw("main_pan"), tg[0], tg[1]);
    for (int c = 0; c < 2; ++c) panG[c].setTargetValue(tg[c]);
    const auto P = readParams();
    for (int c = 0; c < nc; ++c) core[c].setParams(P);
    float* d[2] = { nullptr, nullptr };
    for (int c = 0; c < nc; ++c) d[c] = buffer.getWritePointer(c);
    float peak = 0.f;
    for (int i = 0; i < n; ++i) {
        const float b = bypassMix.getNextValue(), g[2] = { panG[0].getNextValue(), panG[1].getNextValue() };
        for (int c = 0; c < nc; ++c) {
            const float in = d[c][i];
            peak = std::max(peak, std::abs(in));
            float& slot = dry[(size_t) c][(size_t) dryPos];
            const float delayed = slot; slot = in;
            const float wet = core[c].process(in) * (nc == 2 ? g[c] : 1.f);
            d[c][i] = wet + (delayed - wet) * b;
        }
        if (++dryPos >= dryLen) dryPos = 0;
    }
    for (int c = nc; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    if (byp && !bypassMix.isSmoothing()) { active = false; vu[0] = vu[1] = -22.f; over = 0.f; return; }
    float sat = -22.f, gr = -22.f, ov = 0.f;
    for (int c = 0; c < nc; ++c) {
        core[c].updateMeters();
        sat = std::max(sat, (float) core[c].vuIn); gr = std::max(gr, (float) core[c].vuOut); ov = std::max(ov, (float) core[c].over);
    }
    vu[0] = sat; vu[1] = gr; over = ov;
    active = peak > 1e-4f;
}

juce::AudioProcessorEditor* M388Processor::createEditor() { return new M388Editor(*this); }

void M388Processor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary(*xml, dest);
}
void M388Processor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new M388Processor(); }
