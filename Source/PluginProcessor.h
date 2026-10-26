#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "GlitchEngine.h"
#include <array>
#include <atomic>

class GlitchLabProcessor : public juce::AudioProcessor
{
public:
    GlitchLabProcessor();
    ~GlitchLabProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "GlitchLab"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    void randomise (float amount);                 // 1 = full randomize, ~0.15 = mutate
    void applyPreset (int index);
    void triggerGlitch (int type = -1) { engine.trigger (type); }
    int getActiveEvent() const { return activeEvent.load(); }

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<bool>, glitch::P_COUNT> locked;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void setNormalised (int index, float actualValue);

    glitch::GlitchEngine engine;
    std::array<std::atomic<float>*, glitch::P_COUNT> raw {};
    std::vector<float> monoSpare;
    std::atomic<int> activeEvent { -1 };
    glitch::Rng uiRng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchLabProcessor)
};
