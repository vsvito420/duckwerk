#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
inline juce::uint32 hash32 (juce::uint32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

inline juce::int64 posMod (juce::int64 a, juce::int64 m)
{
    const auto r = a % m;
    return r < 0 ? r + m : r;
}

inline void maxInto (std::atomic<float>& a, float v)
{
    if (v > a.load (std::memory_order_relaxed))
        a.store (v, std::memory_order_relaxed);
}

const double kLengths[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0 };
const int kLoopLengths[] = { 4, 8, 16, 32, 0 };
} // namespace

double DuckwerkProcessor::lengthInBeats (int i)
{
    return kLengths[juce::jlimit (0, (int) std::size (kLengths) - 1, i)];
}

juce::StringArray DuckwerkProcessor::lengthNames()
{
    return { "1/4 Beat", "1/2 Beat", "3/4 Beat", "1 Beat", "1.5 Beats", "2 Beats", "3 Beats", "4 Beats (1 Bar)", "8 Beats (2 Bars)" };
}

juce::AudioProcessorValueTreeState::ParameterLayout DuckwerkProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto f = [] (const char* id, const char* name, float lo, float hi, float step, float def, float skewMid = 0.0f)
    {
        NormalisableRange<float> r (lo, hi, step);
        if (skewMid > 0.0f)
            r.setSkewForCentre (skewMid);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def);
    };

    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode", StringArray { "Sync", "Trigger", "Envelope" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "length", 1 }, "Length", lengthNames(), 3));
    l.add (f ("depth", "Depth", 0.0f, 100.0f, 0.1f, 100.0f));
    l.add (f ("smooth", "Smooth", 0.1f, 20.0f, 0.1f, 1.5f, 3.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "lowonly", 1 }, "Low Only", false));
    l.add (f ("xover", "Crossover", 60.0f, 600.0f, 1.0f, 180.0f, 180.0f));
    l.add (f ("thresh", "Threshold", -48.0f, 0.0f, 0.1f, -18.0f));
    l.add (f ("hold", "Hold", 10.0f, 500.0f, 1.0f, 100.0f, 100.0f));
    l.add (f ("release", "Release", 10.0f, 500.0f, 1.0f, 120.0f, 120.0f));
    l.add (f ("vardepth", "Var Depth", 0.0f, 100.0f, 0.1f, 0.0f));
    l.add (f ("varshape", "Var Shape", 0.0f, 100.0f, 0.1f, 0.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "loop", 1 }, "Var Repeat", StringArray { "4", "8", "16", "32", "Never" }, 2));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "seed", 1 }, "Seed", 1, 64, 1));
    l.add (f ("harm", "Harmonics", 0.0f, 100.0f, 0.1f, 0.0f));
    l.add (f ("harmfreq", "Harm Freq", 50.0f, 250.0f, 1.0f, 110.0f, 110.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "check", 1 }, "Speaker Check", false));
    return l;
}

DuckwerkProcessor::DuckwerkProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DUCKWERK", createLayout())
{
    for (auto& a : accents)
        a.store (1.0f);

    pMode = apvts.getRawParameterValue ("mode");
    pLength = apvts.getRawParameterValue ("length");
    pDepth = apvts.getRawParameterValue ("depth");
    pSmooth = apvts.getRawParameterValue ("smooth");
    pLowOnly = apvts.getRawParameterValue ("lowonly");
    pXover = apvts.getRawParameterValue ("xover");
    pThresh = apvts.getRawParameterValue ("thresh");
    pHold = apvts.getRawParameterValue ("hold");
    pRelease = apvts.getRawParameterValue ("release");
    pVarDepth = apvts.getRawParameterValue ("vardepth");
    pVarShape = apvts.getRawParameterValue ("varshape");
    pLoop = apvts.getRawParameterValue ("loop");
    pSeed = apvts.getRawParameterValue ("seed");
    pHarm = apvts.getRawParameterValue ("harm");
    pHarmFreq = apvts.getRawParameterValue ("harmfreq");
    pCheck = apvts.getRawParameterValue ("check");

    setNodes (dw::presets().front().nodes);
}

bool DuckwerkProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! (sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo()))
            return false;
    }
    return true;
}

void DuckwerkProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    juce::dsp::ProcessSpec stereo { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 2 };
    juce::dsp::ProcessSpec mono { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), 1 };

    splitFilter.prepare (stereo);
    checkFilter.prepare (stereo);
    checkFilter.setType (juce::dsp::LinkwitzRileyFilterType::highpass);
    checkFilter.setCutoffFrequency (160.0f);

    harmLow.prepare (mono);
    harmLow.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
    for (auto* flt : { &harmHp1, &harmHp2, &harmLp })
    {
        flt->prepare (mono);
        flt->setResonance (0.7071f);
    }
    harmHp1.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    harmHp2.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    harmLp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    auto lp = juce::dsp::IIR::Coefficients<float>::makeLowPass (sampleRate, 150.0);
    visKickLp.coefficients = lp;
    visPostLp.coefficients = lp;
    visKickLp.reset();
    visPostLp.reset();

    gain = 1.0f;
    fastEnv = slowEnv = followEnv = harmEnv = 0.0f;
    cycleValid = false;
    trigActive = false;
    visValid = false;
    lastMode = -1;
}

void DuckwerkProcessor::startCycle (juce::int64 k, int loopLen, int seed, float varShape)
{
    cycleIndex = k;
    cycStep = (int) posMod (k, numAccents);
    const auto km = (juce::uint32) (loopLen > 0 ? posMod (k, loopLen) : k);
    const auto h1 = hash32 (km * 2654435761U + (juce::uint32) seed * 40503U + 1U);
    const auto h2 = hash32 (h1 + 0x9e3779b9U);
    cycU = (float) h1 / 4294967296.0f;
    cycR = (float) h2 / 2147483648.0f - 1.0f;
    cycWarp = std::exp2 (cycR * varShape);
    vis.step.store (cycStep, std::memory_order_relaxed);
}

void DuckwerkProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    auto* mainBus = getBus (true, 0);
    const int nch = juce::jmin (2, mainBus != nullptr ? mainBus->getNumberOfChannels() : 0, buffer.getNumChannels());
    if (nch == 0 || numSamples == 0)
        return;

    float* d0 = buffer.getWritePointer (0);
    float* d1 = nch > 1 ? buffer.getWritePointer (1) : nullptr;

    const float* scL = nullptr;
    const float* scR = nullptr;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        const int idx = scBus->getChannelIndexInProcessBlockBuffer (0);
        const int c = scBus->getNumberOfChannels();
        if (c > 0 && idx + c <= buffer.getNumChannels())
        {
            scL = buffer.getReadPointer (idx);
            scR = c > 1 ? buffer.getReadPointer (idx + 1) : scL;
        }
    }

    // Transport
    double bpm = 120.0, ppq = 0.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm(); b.hasValue() && *b > 1.0)
                bpm = *b;
            if (auto q = pos->getPpqPosition())
            {
                ppq = *q;
                playing = pos->getIsPlaying();
            }
        }
    const double ppqPerSample = bpm / 60.0 / sr;

    // Parameters
    const int mode = (int) pMode->load();
    const double lenBeats = lengthInBeats ((int) pLength->load());
    const double lenSamples = juce::jmax (16.0, lenBeats * 60.0 / bpm * sr);
    const double phaseInc = 1.0 / lenSamples;
    const float depth = pDepth->load() * 0.01f;
    const float smoothCoef = 1.0f - std::exp (-1.0f / (pSmooth->load() * 0.001f * (float) sr));
    const bool lowOnly = pLowOnly->load() > 0.5f;
    const float thrDb = pThresh->load();
    const float thr = juce::Decibels::decibelsToGain (thrDb);
    const auto holdSamples = (juce::int64) (pHold->load() * 0.001 * sr);
    const float envRel = 1.0f - std::exp (-1.0f / (pRelease->load() * 0.001f * (float) sr));
    const float envAtt = 1.0f - std::exp (-1.0f / (0.0004f * (float) sr));
    const float varDepth = pVarDepth->load() * 0.01f;
    const float varShape = pVarShape->load() * 0.01f;
    const int loopLen = kLoopLengths[juce::jlimit (0, 4, (int) pLoop->load())];
    const int seed = (int) pSeed->load();
    const float harmAmt = pHarm->load() * 0.01f * 1.5f;
    const float harmFreq = pHarmFreq->load();
    const bool check = pCheck->load() > 0.5f;

    const float fastRel = std::exp (-1.0f / (0.003f * (float) sr));
    const float slowCoef = 1.0f - std::exp (-1.0f / (0.04f * (float) sr));
    const float harmRel = std::exp (-1.0f / (0.05f * (float) sr));

    splitFilter.setCutoffFrequency (pXover->load());
    harmLow.setCutoffFrequency (harmFreq);
    harmHp1.setCutoffFrequency (harmFreq);
    harmHp2.setCutoffFrequency (harmFreq);
    harmLp.setCutoffFrequency (juce::jmin (harmFreq * 6.0f, (float) sr * 0.45f));

    if (mode != lastMode)
    {
        lastMode = mode;
        cycleValid = false;
        trigActive = false;
        visValid = false;
    }

    cycWarp = std::exp2 (cycR * varShape);
    const bool useWarp = varShape > 0.001f;
    const bool useHarm = harmAmt > 0.0f;
    if (useHarm != harmWasOn)
    {
        harmWasOn = useHarm;
        harmLow.reset();
        harmHp1.reset();
        harmHp2.reset();
        harmLp.reset();
        harmEnv = 0.0f;
    }
    // The display costs CPU, so it only records while the editor is open.
    const bool capture = editorOpen.load (std::memory_order_relaxed) && ! vis.freeze.load (std::memory_order_relaxed);
    const double samplesPerCol = juce::jmax (1.0, (double) histBeats.load() * 60.0 / bpm * sr / (double) dw::VisData::cols);
    float visPhase = -1.0f;
    float blockPeak = 0.0f, scPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double q = ppq + (double) i * ppqPerSample;
        const float inL = d0[i];
        const float inR = d1 != nullptr ? d1[i] : inL;
        const float kick = scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f;
        const float det = std::abs (kick);
        scPeak = juce::jmax (scPeak, det);

        fastEnv = det > fastEnv ? det : fastEnv * fastRel;
        slowEnv += (det - slowEnv) * slowCoef;

        float target = 1.0f;
        visPhase = -1.0f;
        juce::int64 visId = 0;

        auto curveGain = [&] (float ph)
        {
            const float d = depth * accents[(size_t) cycStep].load (std::memory_order_relaxed) * (1.0f - 0.5f * varDepth * cycU);
            const float warped = useWarp ? std::pow (ph, cycWarp) : ph;
            return 1.0f - d * (1.0f - table.lookup (warped));
        };

        if (mode == Sync)
        {
            if (playing)
            {
                const double pos = q / lenBeats;
                const double fl = std::floor (pos);
                const auto k = (juce::int64) fl;
                if (! cycleValid || k != cycleIndex)
                {
                    cycleValid = true;
                    startCycle (k, loopLen, seed, varShape);
                }
                const float ph = (float) (pos - fl);
                target = curveGain (ph);
                visPhase = ph;
                visId = k;
            }
            else
            {
                cycleValid = false;
            }
        }
        else if (mode == Trigger)
        {
            if (sinceTrig < (juce::int64) 1 << 40)
                ++sinceTrig;
            if (fastEnv > thr && fastEnv > 2.0f * slowEnv && sinceTrig >= holdSamples)
            {
                sinceTrig = 0;
                trigPhase = 0.0;
                trigActive = true;
                startCycle (playing ? (juce::int64) std::llround (q / lenBeats) : ++freeCount, loopLen, seed, varShape);
                triggerCount.fetch_add (1, std::memory_order_relaxed);
            }
            if (trigActive)
            {
                target = curveGain ((float) trigPhase);
                if (trigPhase < 1.0)
                {
                    visPhase = (float) trigPhase;
                    visId = (juce::int64) triggerCount.load (std::memory_order_relaxed);
                }
                trigPhase = juce::jmin (1.0, trigPhase + phaseInc);
            }
        }
        else
        {
            followEnv += (det - followEnv) * (det > followEnv ? envAtt : envRel);
            // Ducking starts at the threshold and reaches full depth 18 dB above it.
            const float over = juce::Decibels::gainToDecibels (followEnv, -100.0f) - thrDb;
            target = 1.0f - depth * juce::jlimit (0.0f, 1.0f, over / 18.0f);
            if (playing)
            {
                const double pos = q / lenBeats;
                const double fl = std::floor (pos);
                visPhase = (float) (pos - fl);
                visId = (juce::int64) fl;
            }
        }

        gain += (target - gain) * smoothCoef;

        float outL, outR;
        if (lowOnly)
        {
            float lo, hi;
            splitFilter.processSample (0, inL, lo, hi);
            outL = lo * gain + hi;
            if (d1 != nullptr)
            {
                splitFilter.processSample (1, inR, lo, hi);
                outR = lo * gain + hi;
            }
            else
            {
                outR = outL;
            }
        }
        else
        {
            outL = inL * gain;
            outR = inR * gain;
        }

        const float preMono = 0.5f * (inL + inR);
        const float postMono = 0.5f * (outL + outR);

        // Harmonics: turn the sub into overtones that small speakers can reproduce.
        if (useHarm)
        {
            const float low = harmLow.processSample (0, postMono);
            const float a = std::abs (low);
            harmEnv = a > harmEnv ? a : harmEnv * harmRel;
            float h = 0.0f;
            if (harmEnv > 1.0e-5f)
            {
                const float n = juce::jlimit (-1.0f, 1.0f, low / harmEnv);
                const float t2 = 2.0f * n * n - 1.0f;
                const float t3 = (4.0f * n * n - 3.0f) * n;
                h = harmEnv * (0.7f * t2 + 0.5f * t3);
            }
            h = harmLp.processSample (0, harmHp2.processSample (0, harmHp1.processSample (0, h)));
            outL += harmAmt * h;
            outR += harmAmt * h;
        }

        if (check)
        {
            outL = checkFilter.processSample (0, outL);
            outR = checkFilter.processSample (1, outR);
        }

        d0[i] = outL;
        if (d1 != nullptr)
            d1[i] = outR;

        // ---- display capture ----
        blockPeak = juce::jmax (blockPeak, det);
        if (capture)
        {
            const float kLow = std::abs (visKickLp.processSample (kick));
            const float pLow = std::abs (visPostLp.processSample (postMono));
            const float aPre = std::abs (preMono), aPost = std::abs (postMono);
            if (visPhase >= 0.0f)
            {
                if (! visValid || visId != lastVisId)
                {
                    visValid = true;
                    lastVisId = visId;
                    visCycle = (visCycle + 1) % dw::VisData::cycles;
                    vis.clearCycle (visCycle);
                    vis.writeCycle.store (visCycle, std::memory_order_relaxed);
                }
                const int b = juce::jmin (dw::VisData::bins - 1, (int) (visPhase * (float) dw::VisData::bins));
                maxInto (vis.at (visCycle, dw::VisData::Kick, b), det);
                maxInto (vis.at (visCycle, dw::VisData::Pre, b), aPre);
                maxInto (vis.at (visCycle, dw::VisData::Post, b), aPost);
                maxInto (vis.at (visCycle, dw::VisData::KickLow, b), kLow);
                maxInto (vis.at (visCycle, dw::VisData::PostLow, b), pLow);
                auto& g = vis.at (visCycle, dw::VisData::Gain, b);
                const float old = g.load (std::memory_order_relaxed);
                if (old < 0.0f || gain < old)
                    g.store (gain, std::memory_order_relaxed);
            }

            if (playing)
            {
                const auto beat = (juce::int64) std::floor (q);
                if (beat != lastBeat)
                {
                    lastBeat = beat;
                    hFlag = juce::jmax (hFlag, posMod (beat, 4) == 0 ? 2.0f : 1.0f);
                }
            }

            if (playing || aPre > 1.0e-4f || det > 1.0e-4f || hCount > 0.0)
            {
                hKick = juce::jmax (hKick, det);
                hPre = juce::jmax (hPre, aPre);
                hPost = juce::jmax (hPost, aPost);
                hGain = juce::jmin (hGain, gain);
                hCount += 1.0;
                if (hCount >= samplesPerCol)
                {
                    hCount -= samplesPerCol;
                    if (hCount >= samplesPerCol)
                        hCount = 0.0;
                    const int w = vis.histWrite.load (std::memory_order_relaxed);
                    vis.h (dw::VisData::HKick, w).store (hKick, std::memory_order_relaxed);
                    vis.h (dw::VisData::HPre, w).store (hPre, std::memory_order_relaxed);
                    vis.h (dw::VisData::HPost, w).store (hPost, std::memory_order_relaxed);
                    vis.h (dw::VisData::HGain, w).store (hGain, std::memory_order_relaxed);
                    vis.h (dw::VisData::HFlag, w).store (hFlag, std::memory_order_relaxed);
                    vis.histWrite.store ((w + 1) % dw::VisData::cols, std::memory_order_relaxed);
                    hKick = hPre = hPost = hFlag = 0.0f;
                    hGain = 1.0f;
                }
            }
        }
    }

    juce::ignoreUnused (blockPeak);
    if (capture)
        vis.phase.store (visPhase, std::memory_order_relaxed);

    if (scPeak > 1.0e-4f)
        scHold = (int) (sr * 0.75);
    else
        scHold = juce::jmax (0, scHold - numSamples);
    vis.scActive.store (scHold > 0, std::memory_order_relaxed);
}

std::vector<dw::Node> DuckwerkProcessor::getNodes() const
{
    const juce::ScopedLock sl (nodeLock);
    return nodes;
}

void DuckwerkProcessor::setNodes (std::vector<dw::Node> n)
{
    const juce::ScopedLock sl (nodeLock);
    nodes = std::move (n);
    table.render (nodes);
    curveVersion.fetch_add (1);
}

void DuckwerkProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("curve", dw::nodesToString (getNodes()), nullptr);
    juce::String acc;
    for (auto& a : accents)
        acc << juce::String (a.load(), 4) << ";";
    state.setProperty ("accents", acc, nullptr);
    state.setProperty ("grid", gridDiv.load(), nullptr);
    state.setProperty ("histBeats", histBeats.load(), nullptr);
    state.setProperty ("presetName", getPresetName(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DuckwerkProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (state.hasProperty ("curve"))
        setNodes (dw::nodesFromString (state["curve"].toString()));
    if (state.hasProperty ("accents"))
    {
        auto toks = juce::StringArray::fromTokens (state["accents"].toString(), ";", "");
        for (int i = 0; i < numAccents && i < toks.size(); ++i)
            if (toks[i].isNotEmpty())
                accents[(size_t) i].store (juce::jlimit (0.0f, 1.0f, toks[i].getFloatValue()));
    }
    gridDiv.store (juce::jlimit (0, 64, (int) state.getProperty ("grid", 16)));
    histBeats.store (juce::jlimit (4, 64, (int) state.getProperty ("histBeats", 16)));
    setPresetName (state.getProperty ("presetName", "").toString());
    apvts.replaceState (state);
    stateVersion.fetch_add (1);
}

juce::String DuckwerkProcessor::getPresetName() const
{
    const juce::ScopedLock sl (nodeLock);
    return presetName;
}

void DuckwerkProcessor::setPresetName (const juce::String& n)
{
    const juce::ScopedLock sl (nodeLock);
    presetName = n;
}

juce::File DuckwerkProcessor::presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Audio/Presets/Duckwerk");
    dir.createDirectory();
    return dir;
}

bool DuckwerkProcessor::savePreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    setPresetName (clean);
    juce::MemoryBlock mb;
    getStateInformation (mb);
    return presetFolder().getChildFile (clean + ".duckwerk").replaceWithData (mb.getData(), mb.getSize());
}

bool DuckwerkProcessor::loadPreset (const juce::File& file)
{
    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb) || mb.getSize() == 0)
        return false;
    setStateInformation (mb.getData(), (int) mb.getSize());
    setPresetName (file.getFileNameWithoutExtension());
    return true;
}

void DuckwerkProcessor::resetToDefault()
{
    for (auto* prm : getParameters())
        prm->setValueNotifyingHost (prm->getDefaultValue());
    for (auto& a : accents)
        a.store (1.0f);
    setNodes (dw::presets().front().nodes);
    setPresetName ({});
    stateVersion.fetch_add (1);
}

juce::AudioProcessorEditor* DuckwerkProcessor::createEditor()
{
    return new DuckwerkEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DuckwerkProcessor();
}
