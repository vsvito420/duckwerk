// Headless check: feeds a synthetic kick (sidechain) and bass (main input) through the
// processor in every mode, prints measurements and renders the editor to PNG files.
#include "../Source/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

namespace
{
constexpr double kSr = 48000.0;
constexpr double kBpm = 150.0;
constexpr int kBlock = 512;

struct FakePlayHead : juce::AudioPlayHead
{
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (kBpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (true);
        return p;
    }
};

struct Result
{
    double rmsDucked = 0.0, rmsOpen = 0.0, fundamental = 0.0, harmonics = 0.0;
    int triggers = 0;
};

void setParam (DuckwerkProcessor& p, const char* id, float realValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

// Runs `beats` beats. Bass = 50 Hz sine; kick = decaying sweep on every beat.
Result run (DuckwerkProcessor& proc, FakePlayHead& ph, int beats, double bassHz = 50.0)
{
    const double beatSamples = 60.0 / kBpm * kSr;
    const auto total = (juce::int64) (beats * beatSamples);
    const int trigBefore = proc.triggerCount.load();
    juce::AudioBuffer<float> buf (4, kBlock);
    double sumD = 0, sumO = 0;
    double re[3] = {}, im[3] = {};
    juce::int64 nD = 0, nO = 0;
    static double bassPhase = 0.0;

    for (juce::int64 pos = 0; pos < total; pos += kBlock)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            const double t = (double) (pos + i);
            const double inBeat = std::fmod (t, beatSamples) / kSr;
            const double f = 45.0 + 130.0 * std::exp (-inBeat / 0.03);
            static double kp = 0.0;
            kp = inBeat < 1.0 / kSr ? 0.0 : kp + juce::MathConstants<double>::twoPi * f / kSr;
            const float kick = (float) (0.9 * std::exp (-inBeat / 0.09) * std::sin (kp));
            bassPhase += juce::MathConstants<double>::twoPi * bassHz / kSr;
            const float bass = (float) (0.5 * std::sin (bassPhase));
            buf.setSample (0, i, bass);
            buf.setSample (1, i, bass);
            buf.setSample (2, i, kick);
            buf.setSample (3, i, kick);
        }
        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
        ph.ppq += kBlock * kBpm / 60.0 / kSr;

        for (int i = 0; i < kBlock; ++i)
        {
            const double phase = std::fmod ((double) (pos + i), beatSamples) / beatSamples;
            const float o = buf.getSample (0, i);
            // single-bin DFT at the fundamental and its 2nd / 3rd harmonic
            for (int h = 0; h < 3; ++h)
            {
                const double w = juce::MathConstants<double>::twoPi * bassHz * (h + 1) * (double) (pos + i) / kSr;
                re[h] += o * std::cos (w);
                im[h] += o * std::sin (w);
            }
            if (phase > 0.01 && phase < 0.06) { sumD += o * o; ++nD; }
            if (phase > 0.7 && phase < 0.95) { sumO += o * o; ++nO; }
        }
    }
    Result r;
    r.rmsDucked = std::sqrt (sumD / (double) juce::jmax ((juce::int64) 1, nD));
    r.rmsOpen = std::sqrt (sumO / (double) juce::jmax ((juce::int64) 1, nO));
    auto mag = [&] (int h) { return 2.0 * std::sqrt (re[h] * re[h] + im[h] * im[h]) / (double) total; };
    r.fundamental = mag (0);
    r.harmonics = mag (1) + mag (2);
    r.triggers = proc.triggerCount.load() - trigBefore;
    return r;
}

void snapshot (DuckwerkProcessor& proc, const juce::File& file, bool showHelp = false)
{
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    if (showHelp)
        for (auto* c : ed->getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "?")
                b->onClick();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream os (file);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
    ed.reset();
    proc.editorOpen.store (true);
    std::cout << "wrote " << file.getFullPathName() << "\n";
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
        if (! ok)
            ++failures;
    };

    DuckwerkProcessor proc;
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    proc.editorOpen.store (true);
    proc.setRateAndBufferSizeDetails (kSr, kBlock);
    proc.prepareToPlay (kSr, kBlock);

    std::cout << "SYNC\n";
    auto r = run (proc, ph, 16);
    std::cout << "  ducked " << r.rmsDucked << "  open " << r.rmsOpen << "\n";
    check (r.rmsDucked < 0.05 && r.rmsOpen > 0.3, "bass is silent on the kick and back at full level later");

    setParam (proc, "vardepth", 70.0f);
    setParam (proc, "varshape", 60.0f);
    proc.accents[2].store (0.5f);
    proc.accents[5].store (0.7f);
    run (proc, ph, 16);
    snapshot (proc, outDir.getChildFile ("sync.png"));
    setParam (proc, "vardepth", 0.0f);
    setParam (proc, "varshape", 0.0f);
    proc.accents[2].store (1.0f);
    proc.accents[5].store (1.0f);

    std::cout << "LOW ONLY (bass 400 Hz, crossover 180 Hz)\n";
    setParam (proc, "lowonly", 1.0f);
    r = run (proc, ph, 8, 400.0);
    std::cout << "  ducked " << r.rmsDucked << "  open " << r.rmsOpen << "\n";
    check (r.rmsDucked > 0.25, "content above the crossover is left alone");
    setParam (proc, "lowonly", 0.0f);

    std::cout << "TRIGGER\n";
    setParam (proc, "mode", 1.0f);
    r = run (proc, ph, 16);
    std::cout << "  triggers " << r.triggers << "  ducked " << r.rmsDucked << "  open " << r.rmsOpen << "\n";
    check (r.triggers == 16, "exactly one trigger per kick");
    check (r.rmsDucked < 0.08 && r.rmsOpen > 0.3, "curve follows the kick");
    snapshot (proc, outDir.getChildFile ("trigger.png"));
    snapshot (proc, outDir.getChildFile ("help.png"), true);

    std::cout << "ENVELOPE\n";
    setParam (proc, "mode", 2.0f);
    r = run (proc, ph, 16);
    std::cout << "  ducked " << r.rmsDucked << "  open " << r.rmsOpen << "\n";
    check (r.rmsDucked < 0.1 && r.rmsOpen > 0.3, "ducking follows the kick level");
    snapshot (proc, outDir.getChildFile ("envelope.png"));

    std::cout << "HARMONICS\n";
    setParam (proc, "mode", 0.0f);
    setParam (proc, "depth", 0.0f);
    const auto dry = run (proc, ph, 8);
    setParam (proc, "harm", 100.0f);
    const auto wet = run (proc, ph, 8);
    std::cout << "  fundamental " << wet.fundamental << "  harmonics off " << dry.harmonics << "  on " << wet.harmonics << "\n";
    check (dry.harmonics < 0.01 * dry.fundamental, "signal is clean with harmonics off");
    check (wet.harmonics > 0.2 * wet.fundamental, "harmonics add audible content above the sub");
    setParam (proc, "check", 1.0f);
    const auto chk = run (proc, ph, 8);
    std::cout << "  speaker check level: " << chk.rmsOpen << " (full " << wet.rmsOpen << ")\n";
    check (chk.rmsOpen < wet.rmsOpen * 0.7, "speaker check removes the sub");
    setParam (proc, "check", 0.0f);
    setParam (proc, "depth", 100.0f);

    std::cout << "STATE\n";
    proc.setNodes (dw::presets()[1].nodes);
    proc.accents[3].store (0.25f);
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    DuckwerkProcessor other;
    other.setStateInformation (mb.getData(), (int) mb.getSize());
    check (other.getNodes().size() == dw::presets()[1].nodes.size(), "curve survives save/load");
    check (std::abs (other.accents[3].load() - 0.25f) < 0.001f, "accents survive save/load");
    check (std::abs (other.apvts.getRawParameterValue ("harm")->load() - 100.0f) < 0.01f, "parameters survive save/load");

    std::cout << (failures == 0 ? "ALL OK\n" : "FAILURES\n");
    return failures;
}
