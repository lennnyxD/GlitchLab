# GlitchLab

A glitch / stutter / tape-stop / pitch-warp effect plugin for FL Studio (VST3, also AU on Mac),
built for vocals, basses and anything else you want to destroy.

Made with [JUCE](https://juce.com) 8. The sound engine is plain C++ with no dependencies
(`Source/GlitchEngine.h`) and has its own tests.

## What it does

It constantly listens to the audio. When a **trigger** fires it grabs a slice of what just played and
plays it back *wrong* for a short moment (50 ms to 1.5 s), then drops back to normal.

**10 glitch types** (set how often each one happens with the *Glitch Types* knobs, 0 = never):

| Type | What you hear |
|---|---|
| Stutter | repeats a tiny slice: "na-na-na-name". *Stutter ramp* makes the repeats speed up or slow down (machine gun) |
| Pitch Jump | the last chunk replayed higher or lower |
| Pitch Swoop | pitch dives or rises while it plays |
| Tape Stop | the audio slows to a halt like a tape machine |
| Speed Up | starts slow and winds up to full speed |
| Reverse | the last chunk backwards |
| Freeze | holds one tiny moment as a sustained buzz |
| Scatter | shattered cloud of tiny random grains, random pitches and pan |
| Dropout | chops holes in the sound like a skipping CD |
| Shuffle | cuts a chunk into pieces and replays them in the wrong order |

**6 destruction FX** are layered on top of the glitches (each event randomly gets some of them, *FX chance* controls how often): Bitcrush, Downsample, Fold distortion, Ring mod, Filter sweep (low/high/band), Auto-pan.

**Triggers**
- **Grid**: fires on the beat (1/4 down to 1/64, triplets), locked to your FL project tempo. *Probability* decides how many steps actually glitch.
- **Transient**: fires when a new word / hit starts. It waits one slice length first, so the stutter repeats the *start of the word* (hence "hi, my name, name, name"). *Sensitivity* sets how easily it reacts. It detects word *starts*, it can't understand which word it is.
- **Grid + Transient**: both.
- **Sequencer**: the 16 step buttons at the bottom choose exactly which steps glitch.
- **Manual only**: use the **GLITCH!** button or MIDI notes (C = Stutter, C# = Pitch Jump, D = Swoop, D# = Tape Stop, E = Speed Up, F = Reverse, F# = Freeze, G = Scatter, G# = Dropout, A = Shuffle, A#/B = random).

**Chaos** is the big one: at 0 every glitch is predictable and centred in your Shape ranges, at 1 every length/pitch/slice is random and glitches chain into each other back to back.

**Randomize / Mutate / Lock**: *Randomize* rolls a whole new sound, *Mutate* nudges the current one a little,
and the **lock** button under any knob keeps it fixed while you roll the dice. Presets and Randomize never touch Mix/Output.

13 built-in presets (Vocal Stutter, Word Chopper, Hyperpop Chaos, Pitch Warp, Tape Stop Outro, Reverse Riser, Bass Wreck, Granular Shatter, Robot Glitch, Machine Gun, Sequenced Mayhem, Lo-Fi Skip, ...).

## Getting the plugin (no coding needed): let GitHub build it

1. Make a free account at github.com and click **New repository** (call it `GlitchLab`, keep it private if you like).
2. Upload everything from this folder: **Add file → Upload files**, drag the contents in, commit.
   If the hidden `.github` folder doesn't upload, click **Add file → Create new file**, type the name
   `.github/workflows/build.yml` and paste in the contents of that file.
3. Open the **Actions** tab. A "Build GlitchLab" run starts by itself (or press *Run workflow*). It takes ~10 minutes.
4. When it's green, open the run and download **GlitchLab-Windows-VST3** (or the macOS one) at the bottom.
5. Unzip and copy the `GlitchLab.vst3` **folder** into `C:\Program Files\Common Files\VST3\`.
6. In FL Studio: **Options → Manage plugins → Find installed plugins** (make sure "Scan" includes that folder), then load GlitchLab on a mixer insert (slot dropdown → More plugins).

## Or build it yourself on Windows

Install Visual Studio (Community, "Desktop development with C++") and CMake, then in a terminal in this folder:

```
cmake -S . -B build
cmake --build build --config Release --target GlitchLab_VST3
```
The plugin ends up in `build\GlitchLab_artefacts\Release\VST3\`. (JUCE is downloaded automatically.)
Add `-DGLITCHLAB_STANDALONE=ON` for a standalone app too.

## Try it without a DAW

`glitch_cli` runs the same engine on WAV files:

```
glitch_cli list                                   # presets and every parameter name
glitch_cli demo some_folder                       # renders demo files
glitch_cli process vocal.wav out.wav --preset 3 --seed 5 --bpm 140
glitch_cli process vocal.wav out.wav --random --seed 12 --set chaos=0.9 --set crush=0.6
```

## Using it in FL Studio

- Put it on the mixer insert of your vocal or bass. Leave Mix at 100% and use *Probability* / *Chaos* to control how much happens.
- Start with the preset *Word Chopper* and set Trigger to *Transient* for vocal stutters, *Hyperpop Chaos* for the wild stuff.
- Automate *Chaos*, *Probability* or *Mix* in FL to build drops and fills. Every knob is automatable.
- Send MIDI to it from a Patcher or a MIDI Out channel (FL has no direct MIDI-to-effect routing) to play glitches by hand.

## Honest limits (v1)

- Mono tracks work; the engine is stereo internally.
- Pitch shifting is "speed based" (like tape/sampler pitching), not formant-preserving. That's the classic hyperpop sound.
- The *lock* states are not saved with the project (the knob values are).
- Not yet tested inside FL Studio itself: the engine passes its automated tests and the plugin code compiles against JUCE 8, but the first real-world test in your DAW is yours. Report anything odd and it can be fixed.

## Project layout

```
Source/ParamSpec.h       every parameter, presets, randomizer
Source/GlitchEngine.h    the DSP (stutter, pitch, tape stop, FX, triggers)
Source/PluginProcessor.* JUCE plugin shell (parameters, MIDI, host tempo)
Source/PluginEditor.*    the interface
tests/test_engine.cpp    165 automated checks of the engine
tools/glitch_cli.cpp     command line renderer
```
