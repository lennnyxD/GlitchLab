#pragma once
#include "PluginProcessor.h"
#include <memory>

// Neon look
class GlitchLook : public juce::LookAndFeel_V4
{
public:
    GlitchLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
};

// One rotary knob with a name above it and a "lock" button below it.
class KnobWithLock : public juce::Component
{
public:
    KnobWithLock (GlitchLabProcessor& p, int paramIndex, juce::Colour accent);
    void resized() override;
    void setLockedLook (bool isLocked);
    void setLocked (bool isLocked) { lock.setToggleState (isLocked, juce::dontSendNotification); setLockedLook (isLocked); }

private:
    GlitchLabProcessor& proc;
    int index;
    juce::Colour accentColour;
    juce::Label name;
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::TextButton lock { "lock" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

class GlitchLabEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlitchLabEditor (GlitchLabProcessor&);
    ~GlitchLabEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    KnobWithLock* addKnob (int paramIndex, juce::Colour accent);
    void unlockAll();

    GlitchLabProcessor& proc;
    GlitchLook look;

    juce::ComboBox presetBox, trigBox, divBox;
    juce::TextButton randomBtn { "RANDOMIZE" }, mutateBtn { "MUTATE" }, glitchBtn { "GLITCH!" }, unlockBtn { "unlock all" };
    juce::Label status, trigLabel, divLabel;

    juce::OwnedArray<KnobWithLock> knobs;
    std::array<KnobWithLock*, glitch::P_COUNT> knobFor {};
    juce::OwnedArray<juce::TextButton> stepButtons;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> stepAttachments;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> trigAttachment, divAttachment;

    juce::Rectangle<int> secTrigger, secShape, secGlitches, secFx, secSeq, secTop;
    int lastActive = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlitchLabEditor)
};
