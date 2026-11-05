#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "TapeCore.h"
#include <array>
#include <vector>

class M388Processor : public juce::AudioProcessor {
public:
    M388Processor();
    ~M388Processor() override;
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "M-388 Tape Console"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;
    // il bypass dell'host comanda direttamente il nostro interruttore BYPASS (stessa latenza, nessun click)
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter("main_bypass"); }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> vu[2] { -20.f, -20.f };
    std::atomic<float> over { 0.f };
    std::atomic<bool> active { false };

private:
    tape::Core core[2];
    std::array<std::vector<float>, 2> dry;          // segnale asciutto ritardato della stessa latenza del nastro
    int dryLen = 1, dryPos = 0;
    juce::SmoothedValue<float> bypassMix, panG[2];
    float raw(const char* id) const { return apvts.getRawParameterValue(id)->load(); }
    tape::Params readParams() const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(M388Processor)
};
