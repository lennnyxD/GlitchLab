#include "PluginEditor.h"

using namespace glitch;

namespace
{
    const juce::Colour kBg      (0xff0c0a14);
    const juce::Colour kPanel   (0xff171326);
    const juce::Colour kPink    (0xffff3cac);
    const juce::Colour kCyan    (0xff4df3ff);
    const juce::Colour kYellow  (0xffffe14d);
    const juce::Colour kViolet  (0xffa56bff);
    const juce::Colour kLime    (0xff9bff5a);
    const juce::Colour kDim     (0xff8a84a8);

    constexpr int kKnobW = 78, kKnobH = 112;
}

// ============================================================== look ======
GlitchLook::GlitchLook()
{
    setColour (juce::ResizableWindow::backgroundColourId, kBg);
    setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, juce::Colours::white);
    setColour (juce::ComboBox::outlineColourId, kViolet.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId, kCyan);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, juce::Colours::white);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kPink.withAlpha (0.5f));
    setColour (juce::TextButton::buttonColourId, kPanel);
    setColour (juce::TextButton::buttonOnColourId, kPink);
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}

void GlitchLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                   float startAngle, float endAngle, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const auto fill = s.findColour (juce::Slider::rotarySliderFillColourId);

    g.setColour (kPanel.brighter (0.08f));
    g.fillEllipse (centre.x - radius + 6.f, centre.y - radius + 6.f, (radius - 6.f) * 2.f, (radius - 6.f) * 2.f);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2.f, radius - 2.f, 0.f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2b2545));
    g.strokePath (track, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path val;
    val.addCentredArc (centre.x, centre.y, radius - 2.f, radius - 2.f, 0.f, startAngle, angle, true);
    g.setColour (fill.withAlpha (0.25f));
    g.strokePath (val, juce::PathStrokeType (8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (fill);
    g.strokePath (val, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path ptr;
    ptr.addRoundedRectangle (-1.5f, -(radius - 8.f), 3.f, (radius - 8.f) * 0.5f, 1.f);
    g.setColour (juce::Colours::white);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

void GlitchLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.f);
    g.setColour (b.getToggleState() ? kPink : kPanel.brighter (highlighted ? 0.2f : 0.05f));
    g.fillRoundedRectangle (r, 5.f);
}

void GlitchLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.f);
    auto c = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bg;
    if (highlighted) c = c.brighter (0.15f);
    if (down) c = c.brighter (0.3f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 6.f);
    g.setColour (kViolet.withAlpha (0.45f));
    g.drawRoundedRectangle (r, 6.f, 1.f);
}

// ============================================================== knob ======
KnobWithLock::KnobWithLock (GlitchLabProcessor& p, int paramIndex, juce::Colour accent)
    : proc (p), index (paramIndex), accentColour (accent)
{
    const auto& spec = paramSpecs()[(size_t) paramIndex];

    name.setText (spec.name, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (juce::FontOptions (12.f));
    name.setColour (juce::Label::textColourId, kDim.brighter (0.4f));
    addAndMakeVisible (name);

    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, kKnobW - 8, 16);
    slider.setDoubleClickReturnValue (true, (double) spec.def);
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, spec.id, slider);

    lock.setClickingTogglesState (true);
    lock.setTooltip ("Lock: Randomize, Mutate and presets will leave this knob alone");
    lock.onClick = [this] { proc.locked[(size_t) index].store (lock.getToggleState()); setLockedLook (lock.getToggleState()); };
    addAndMakeVisible (lock);
    setLockedLook (false);
}

void KnobWithLock::setLockedLook (bool isLocked)
{
    slider.setColour (juce::Slider::rotarySliderFillColourId, isLocked ? kDim : accentColour);
    lock.setColour (juce::TextButton::buttonOnColourId, kYellow);
    lock.setButtonText (isLocked ? "locked" : "lock");
    slider.repaint();
}

void KnobWithLock::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (16));
    lock.setBounds (r.removeFromBottom (16).reduced (10, 0));
    slider.setBounds (r);
}

// ============================================================ editor ======
KnobWithLock* GlitchLabEditor::addKnob (int paramIndex, juce::Colour accent)
{
    auto* k = knobs.add (new KnobWithLock (proc, paramIndex, accent));
    knobFor[(size_t) paramIndex] = k;
    addAndMakeVisible (k);
    return k;
}

GlitchLabEditor::GlitchLabEditor (GlitchLabProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);

    // top bar
    for (size_t i = 0; i < presets().size(); ++i) presetBox.addItem (presets()[i].name, (int) i + 1);
    presetBox.setTextWhenNothingSelected ("Presets...");
    presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id > 0) proc.applyPreset (id - 1); };
    addAndMakeVisible (presetBox);

    randomBtn.onClick = [this] { proc.randomise (1.f); };
    mutateBtn.onClick = [this] { proc.randomise (0.15f); };
    glitchBtn.setColour (juce::TextButton::buttonColourId, kPink);
    glitchBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    glitchBtn.onClick = [this] { proc.triggerGlitch (-1); };
    unlockBtn.onClick = [this] { unlockAll(); };
    for (auto* b : { &randomBtn, &mutateBtn, &glitchBtn, &unlockBtn }) addAndMakeVisible (b);

    status.setJustificationType (juce::Justification::centredRight);
    status.setFont (juce::FontOptions (14.f, juce::Font::bold));
    addAndMakeVisible (status);

    addKnob (P_MIX, kCyan);
    addKnob (P_GAIN, kCyan);

    // trigger section
    trigLabel.setText ("Trigger", juce::dontSendNotification);
    divLabel.setText ("Grid", juce::dontSendNotification);
    for (auto* l : { &trigLabel, &divLabel }) { l->setColour (juce::Label::textColourId, kDim.brighter (0.4f)); l->setFont (juce::FontOptions (12.f)); addAndMakeVisible (l); }
    const auto& spT = paramSpecs()[P_TRIGMODE];
    const auto& spD = paramSpecs()[P_DIV];
    trigBox.addItemList (juce::StringArray::fromTokens (spT.choices, "|", ""), 1);
    divBox.addItemList (juce::StringArray::fromTokens (spD.choices, "|", ""), 1);
    addAndMakeVisible (trigBox);
    addAndMakeVisible (divBox);
    trigAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, spT.id, trigBox);
    divAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, spD.id, divBox);
    addKnob (P_PROB, kPink); addKnob (P_SENS, kPink); addKnob (P_CHAOS, kPink);

    // shape section
    for (int i : { P_EVMIN, P_EVMAX, P_SLMIN, P_SLMAX, P_PITCH, P_RAMP }) addKnob (i, kCyan);

    // glitch types
    const juce::Colour cols[EV_COUNT] = { kPink, kViolet, kCyan, kYellow, kLime, kPink, kViolet, kCyan, kYellow, kLime };
    for (int i = 0; i < EV_COUNT; ++i) addKnob (P_W_STUTTER + i, cols[i]);

    // fx
    for (int i = P_CRUSH; i <= P_FXCHANCE; ++i) addKnob (i, i == P_FXCHANCE ? kDim.brighter (0.5f) : kLime);

    // sequencer
    for (int i = 0; i < 16; ++i)
    {
        auto* b = stepButtons.add (new juce::TextButton (juce::String (i + 1)));
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, (i % 4 == 0) ? kYellow : kPink);
        addAndMakeVisible (b);
        stepAttachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            proc.apvts, paramSpecs()[(size_t) (P_STEP1 + i)].id, *b));
    }

    // Size is set last: this triggers resized(), which needs every knob and button to exist already.
    setSize (960, 660);
    startTimerHz (20);
}

GlitchLabEditor::~GlitchLabEditor() { setLookAndFeel (nullptr); }

void GlitchLabEditor::unlockAll()
{
    for (int i = 0; i < P_COUNT; ++i)
    {
        proc.locked[(size_t) i].store (false);
        if (knobFor[(size_t) i] != nullptr) knobFor[(size_t) i]->setLocked (false);
    }
}

void GlitchLabEditor::timerCallback()
{
    const int a = proc.getActiveEvent();
    if (a == lastActive) return;
    lastActive = a;
    if (a >= 0)
    {
        status.setText (juce::String ("GLITCHING: ") + eventName (a), juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, kPink);
    }
    else
    {
        status.setText ("listening...", juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, kDim);
    }
}

void GlitchLabEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setGradientFill (juce::ColourGradient (kPink.withAlpha (0.18f), 0.f, 0.f, kCyan.withAlpha (0.10f), (float) getWidth(), (float) getHeight(), false));
    g.fillRect (getLocalBounds());

    auto section = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        g.setColour (kPanel.withAlpha (0.92f));
        g.fillRoundedRectangle (r.toFloat(), 10.f);
        g.setColour (c.withAlpha (0.55f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.f, 1.2f);
        g.setColour (c);
        g.setFont (juce::FontOptions (12.f, juce::Font::bold));
        g.drawText (title, r.getX() + 12, r.getY() + 4, r.getWidth() - 24, 16, juce::Justification::centredLeft);
    };
    section (secTrigger, "TRIGGER", kPink);
    section (secShape, "SHAPE", kCyan);
    section (secGlitches, "GLITCH TYPES  (0 = never, 1 = often)", kViolet);
    section (secFx, "DESTRUCTION FX  (applied on top of every glitch)", kLime);
    section (secTop, "OUTPUT", kCyan);
    section (secSeq, "16-STEP SEQUENCER  (Trigger = Sequencer)  -  MIDI notes C..A also fire glitches", kYellow);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (30.f, juce::Font::bold));
    g.drawText ("GLITCH", 18, 12, 120, 40, juce::Justification::centredLeft);
    g.setColour (kPink);
    g.drawText ("LAB", 128, 12, 80, 40, juce::Justification::centredLeft);
}

void GlitchLabEditor::resized()
{
    // Safety: do nothing until the whole interface has been built.
    if (stepButtons.size() < 16) return;
    for (int id : { P_MIX, P_GAIN, P_PROB, P_SENS, P_CHAOS, P_EVMIN, P_EVMAX, P_SLMIN, P_SLMAX, P_PITCH, P_RAMP,
                    P_W_STUTTER, P_W_SHUFFLE, P_CRUSH, P_FXCHANCE })
        if (knobFor[(size_t) id] == nullptr) return;

    auto r = getLocalBounds().reduced (14);

    // ---- top bar ----
    auto top = r.removeFromTop (56);
    top.removeFromLeft (200);
    presetBox.setBounds (top.removeFromLeft (230).reduced (0, 12));
    top.removeFromLeft (8);
    randomBtn.setBounds (top.removeFromLeft (100).reduced (0, 12));
    top.removeFromLeft (6);
    mutateBtn.setBounds (top.removeFromLeft (80).reduced (0, 12));
    top.removeFromLeft (6);
    glitchBtn.setBounds (top.removeFromLeft (90).reduced (0, 12));
    top.removeFromLeft (6);
    unlockBtn.setBounds (top.removeFromLeft (86).reduced (0, 16));
    status.setBounds (top.removeFromRight (250).withTrimmedBottom (4));
    r.removeFromTop (6);

    // ---- helper for knob rows ----
    auto placeRow = [&] (juce::Rectangle<int> area, std::initializer_list<int> ids)
    {
        for (int id : ids)
        {
            knobFor[(size_t) id]->setBounds (area.removeFromLeft (kKnobW).withHeight (kKnobH));
        }
    };

    // ---- row 1: trigger + shape ----
    auto row1 = r.removeFromTop (kKnobH + 28);
    secTrigger = row1.removeFromLeft (440);
    row1.removeFromLeft (12);
    secShape = row1;
    {
        auto a = secTrigger.withTrimmedTop (22).reduced (8, 0);
        auto combos = a.removeFromLeft (128);
        trigLabel.setBounds (combos.removeFromTop (16));
        trigBox.setBounds (combos.removeFromTop (26));
        combos.removeFromTop (6);
        divLabel.setBounds (combos.removeFromTop (16));
        divBox.setBounds (combos.removeFromTop (26));
        placeRow (a, { P_PROB, P_SENS, P_CHAOS });
        placeRow (secShape.withTrimmedTop (22).reduced (6, 0), { P_EVMIN, P_EVMAX, P_SLMIN, P_SLMAX, P_PITCH, P_RAMP });
    }
    r.removeFromTop (10);

    // ---- row 2: glitch types ----
    secGlitches = r.removeFromTop (kKnobH + 28);
    placeRow (secGlitches.withTrimmedTop (22).reduced (8, 0), { P_W_STUTTER, P_W_PITCHJUMP, P_W_SWOOP, P_W_TAPESTOP, P_W_SPEEDUP,
                                                                P_W_REVERSE, P_W_FREEZE, P_W_SCATTER, P_W_DROPOUT, P_W_SHUFFLE });
    r.removeFromTop (10);

    // ---- row 3: fx + output ----
    auto row3 = r.removeFromTop (kKnobH + 28);
    secFx = row3.removeFromLeft (8 * kKnobW + 28);
    row3.removeFromLeft (12);
    placeRow (secFx.withTrimmedTop (22).reduced (8, 0), { P_CRUSH, P_DOWNSAMPLE, P_FOLD, P_RING, P_FILTER, P_FLIP, P_FXCHANCE });
    secTop = row3;
    placeRow (secTop.withTrimmedTop (22).reduced (8, 0), { P_MIX, P_GAIN });
    r.removeFromTop (10);

    // ---- row 4: sequencer ----
    secSeq = r.removeFromTop (74);
    {
        auto a = secSeq.withTrimmedTop (24).reduced (10, 6);
        const int w = a.getWidth() / 16;
        for (int i = 0; i < 16; ++i)
            stepButtons[i]->setBounds (a.removeFromLeft (w).reduced (2, 0));
    }
}
