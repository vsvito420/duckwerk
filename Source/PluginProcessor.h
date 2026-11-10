#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Curve.h"
#include "Vis.h"

class DuckwerkProcessor : public juce::AudioProcessor
{
public:
    enum Mode { Sync = 0, Trigger = 1, Envelope = 2 };
    static constexpr int numAccents = 8;

    DuckwerkProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Duckwerk"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Curve access for the editor (message thread).
    std::vector<dw::Node> getNodes() const;
    void setNodes (std::vector<dw::Node>);

    static double lengthInBeats (int choiceIndex);
    static juce::StringArray lengthNames();

    juce::AudioProcessorValueTreeState apvts;
    dw::CurveTable table;
    dw::VisData vis;
    std::array<std::atomic<float>, numAccents> accents;
    std::atomic<int> gridDiv { 16 };
    std::atomic<int> histBeats { 16 };
    std::atomic<int> curveVersion { 0 };
    std::atomic<int> triggerCount { 0 };
    std::atomic<int> stateVersion { 0 };
    std::atomic<bool> editorOpen { false };

    // User presets: whole plugin state as files in ~/Library/Audio/Presets/Duckwerk.
    static juce::File presetFolder();
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::File&);
    void resetToDefault();
    juce::String getPresetName() const;
    void setPresetName (const juce::String&);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void startCycle (juce::int64 k, int loopLen, int seed, float varShape);
    juce::String presetName;

    juce::CriticalSection nodeLock;
    std::vector<dw::Node> nodes;

    std::atomic<float>* pMode = nullptr, *pLength = nullptr, *pDepth = nullptr, *pSmooth = nullptr,
                      *pLowOnly = nullptr, *pXover = nullptr, *pThresh = nullptr, *pHold = nullptr,
                      *pRelease = nullptr, *pVarDepth = nullptr, *pVarShape = nullptr, *pLoop = nullptr,
                      *pSeed = nullptr, *pHarm = nullptr, *pHarmFreq = nullptr, *pCheck = nullptr;

    double sr = 48000.0;

    // Ducking state
    int lastMode = -1;
    bool cycleValid = false;
    juce::int64 cycleIndex = 0;
    int cycStep = 0;
    float cycU = 0.0f, cycR = 0.0f, cycWarp = 1.0f;
    bool harmWasOn = false;
    float gain = 1.0f;
    float fastEnv = 0.0f, slowEnv = 0.0f, followEnv = 0.0f;
    double trigPhase = 1.0;
    bool trigActive = false;
    juce::int64 sinceTrig = 1 << 30;
    juce::int64 freeCount = 0;

    juce::dsp::LinkwitzRileyFilter<float> splitFilter;

    // Harmonics for small speakers
    juce::dsp::LinkwitzRileyFilter<float> harmLow;
    juce::dsp::StateVariableTPTFilter<float> harmHp1, harmHp2, harmLp;
    float harmEnv = 0.0f;
    juce::dsp::LinkwitzRileyFilter<float> checkFilter;

    // Display capture
    juce::dsp::IIR::Filter<float> visKickLp, visPostLp;
    bool visValid = false;
    juce::int64 lastVisId = 0;
    int visCycle = 0;
    float hKick = 0.0f, hPre = 0.0f, hPost = 0.0f, hGain = 1.0f, hFlag = 0.0f;
    double hCount = 0.0;
    juce::int64 lastBeat = 0;
    int scHold = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckwerkProcessor)
};
