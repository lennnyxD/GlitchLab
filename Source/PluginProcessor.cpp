#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace glitch;

juce::AudioProcessorValueTreeState::ParameterLayout GlitchLabProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    for (int i = 0; i < P_COUNT; ++i)
    {
        const auto& s = paramSpecs()[(size_t) i];
        const juce::ParameterID pid { s.id, 1 };

        if (s.kind == K_CHOICE)
        {
            params.push_back (std::make_unique<juce::AudioParameterChoice> (
                pid, s.name, juce::StringArray::fromTokens (s.choices, "|", ""), (int) s.def));
        }
        else if (s.kind == K_BOOL)
        {
            params.push_back (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
        }
        else
        {
            juce::NormalisableRange<float> range (s.min, s.max);
            juce::String label;
            if (i == P_GAIN) label = "dB";
            if (i == P_EVMIN || i == P_EVMAX || i == P_SLMIN || i == P_SLMAX) { range.setSkewForCentre (std::sqrt (s.min * s.max) * 1.5f); label = "ms"; }
            if (i == P_PITCH) label = "st";
            params.push_back (std::make_unique<juce::AudioParameterFloat> (
                pid, s.name, range, s.def, juce::AudioParameterFloatAttributes().withLabel (label)));
        }
    }
    return { params.begin(), params.end() };
}

GlitchLabProcessor::GlitchLabProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GlitchLabState", createLayout())
{
    for (int i = 0; i < P_COUNT; ++i)
    {
        raw[(size_t) i] = apvts.getRawParameterValue (paramSpecs()[(size_t) i].id);
        locked[(size_t) i].store (false);
    }
    uiRng.seed ((uint32_t) juce::Time::getMillisecondCounter() | 1u);
}

void GlitchLabProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    engine.seed ((uint32_t) juce::Time::getMillisecondCounter() | 1u);
    monoSpare.assign ((size_t) std::max (samplesPerBlock, 1), 0.f);
}

bool GlitchLabProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return out == layouts.getMainInputChannelSet();
}

void GlitchLabProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = buffer.getNumChannels();
    if (numCh < 1 || n < 1) return;

    // parameters -> engine
    ParamValues v;
    for (int i = 0; i < P_COUNT; ++i) v[(size_t) i] = raw[(size_t) i]->load();
    engine.setParams (v);

    // host tempo / position so the grid locks to the project
    double bpm = 120.0, ppq = 0.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            bpm = pos->getBpm().orFallback (120.0);
            ppq = pos->getPpqPosition().orFallback (0.0);
            playing = pos->getIsPlaying();
        }
    engine.setTransport (bpm, ppq, playing);

    // MIDI notes trigger glitches: C = Stutter, C# = Pitch Jump, D = Swoop, ... A = Shuffle, A#/B = random
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int k = msg.getNoteNumber() % 12;
            engine.trigger (k < EV_COUNT ? k : -1);
        }
    }
    midi.clear();

    float* L = buffer.getWritePointer (0);
    if (numCh >= 2)
    {
        engine.process (L, buffer.getWritePointer (1), n);
    }
    else
    {
        if ((int) monoSpare.size() < n) monoSpare.resize ((size_t) n);
        std::copy (L, L + n, monoSpare.begin());
        engine.process (L, monoSpare.data(), n);
    }
    activeEvent.store (engine.currentEvent());
}

// ------------------------------------------------------------------ state ---
void GlitchLabProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void GlitchLabProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// -------------------------------------------------- randomize and presets ---
void GlitchLabProcessor::setNormalised (int index, float actualValue)
{
    if (auto* p = apvts.getParameter (paramSpecs()[(size_t) index].id))
        p->setValueNotifyingHost (p->convertTo0to1 (actualValue));
}

void GlitchLabProcessor::randomise (float amount)
{
    ParamValues v;
    std::array<bool, P_COUNT> lk {};
    for (int i = 0; i < P_COUNT; ++i)
    {
        v[(size_t) i] = raw[(size_t) i]->load();
        lk[(size_t) i] = locked[(size_t) i].load();
    }
    const ParamValues before = v;
    randomizeParams (v, lk, uiRng, amount);
    for (int i = 0; i < P_COUNT; ++i)
        if (v[(size_t) i] != before[(size_t) i]) setNormalised (i, v[(size_t) i]);
}

void GlitchLabProcessor::applyPreset (int index)
{
    const ParamValues v = presetValues (index);
    for (int i = 0; i < P_COUNT; ++i)
    {
        if (i == P_MIX || i == P_GAIN) continue;        // presets never change your levels
        if (locked[(size_t) i].load()) continue;        // locked knobs survive preset changes too
        setNormalised (i, v[(size_t) i]);
    }
}

juce::AudioProcessorEditor* GlitchLabProcessor::createEditor() { return new GlitchLabEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GlitchLabProcessor(); }
