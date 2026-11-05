#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Geometry.h"
#include <vector>

class M388Editor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit M388Editor(M388Processor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct Ctl { const KnobDef* def; juce::RangedAudioParameter* par; };
    M388Processor& proc;
    juce::Image bg, spr, micOff, micOn, bypOff, bypOn;
    std::vector<Ctl> ctl;
    juce::RangedAudioParameter *calPar = nullptr, *chPar = nullptr, *faderPar = nullptr, *bypassPar = nullptr, *micPar = nullptr;
    int dragIdx = -1;
    bool dragFader = false, lastFine = false;
    float dragStart = 0.f, dragY0 = 0.f, faderDb0 = 0.f;
    int shown = -1;                              // controllo di cui si mostra la targhetta (ctl.size() = fader)
    float shownAlpha = 0.f;
    juce::uint32 touchMs = 0;
    void touch(int idx);
    std::vector<float> lastVals;                 // ultimi valori disegnati (per ridisegnare anche con l'automazione)
    float lastVu[2] { -99.f, -99.f }, lastOver = -1.f;

    float scale() const { return (float) getWidth() / IMG_W; }
    int hitKnob(juce::Point<float> pt) const;
    static bool hitFader(juce::Point<float> pt);
    juce::Rectangle<int> toScreen(juce::Rectangle<float> r) const;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(M388Editor)
};
