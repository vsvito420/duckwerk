#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

namespace dwui
{
struct Look : juce::LookAndFeel_V4
{
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
};

// Curve editor with the remembered kick/bass cycles drawn behind it.
class CurveView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CurveView (DuckwerkProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override { cacheDirty = true; }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void syncFromProcessor();
    juce::uint32 signature() const;

    std::function<void()> onEdit;

private:
    juce::Rectangle<float> plot() const;
    juce::Point<float> toPx (float x, float y) const;
    int hitNode (juce::Point<float>) const;
    int hitHandle (juce::Point<float>) const;
    bool handlePos (int seg, juce::Point<float>& out) const;
    float snapX (float x, const juce::MouseEvent&) const;
    void commit();
    void renderCache (float scale);
    juce::Path layerPath (int cycle, int layer, int layer2 = -1) const;
    juce::Path gainPath (int cycle) const;

    DuckwerkProcessor& proc;
    std::vector<dw::Node> nodes;
    int seenVersion = -1;
    int dragNode = -1, dragHandle = -1;
    float dragStartC = 0.0f;

    // Everything except the cycle being recorded is drawn once per cycle into this image.
    juce::Image cache;
    bool cacheDirty = true;
    int cachedCycle = -1, cachedGrid = -1;
    float cachedScale = 0.0f;
    float norm = 0.05f;
    int clashPct = -1;
};

class HistoryView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit HistoryView (DuckwerkProcessor& p) : proc (p) { setOpaque (true); }
    void paint (juce::Graphics&) override;
    juce::uint32 signature() const;

private:
    DuckwerkProcessor& proc;
    float norm = 0.05f;
};

class AccentView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit AccentView (DuckwerkProcessor& p) : proc (p) { setOpaque (true); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { edit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { edit (e); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    juce::uint32 signature() const;

private:
    juce::Rectangle<float> cell (int i) const;
    void edit (const juce::MouseEvent&);
    DuckwerkProcessor& proc;
};

// Quick-start text shown over the curve; any click closes it.
class HelpOverlay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }
};
} // namespace dwui

class DuckwerkEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DuckwerkEditor (DuckwerkProcessor&);
    ~DuckwerkEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<APVTS::SliderAttachment> att;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& id, const juce::String& name, const juce::String& suffix, const juce::String& tip);
    juce::Rectangle<int> slot (int i) const;
    void refreshPresetList();
    void presetChosen();
    void askSavePreset();

    DuckwerkProcessor& proc;
    dwui::Look look;

    dwui::CurveView curveView;
    dwui::AccentView accentView;
    dwui::HistoryView historyView;
    dwui::HelpOverlay help;

    juce::TextButton modeButtons[3];
    std::unique_ptr<juce::ParameterAttachment> modeAtt;
    juce::ComboBox lengthBox, shapeBox, presetBox, gridBox, histBox, loopBox;
    juce::Label loopLabel;
    juce::TextButton saveButton { "Save" }, helpButton { "?" }, freezeButton { "Freeze" }, lowOnlyButton { "LOW ONLY" }, checkButton { "PREVIEW" };
    std::unique_ptr<APVTS::ComboBoxAttachment> lengthAtt, loopAtt;
    std::unique_ptr<APVTS::ButtonAttachment> lowOnlyAtt, checkAtt;
    juce::Array<juce::File> presetFiles;

    Knob depth, smooth, xover, thresh, hold, release, varDepth, varShape, seed, harm, harmFreq;

    int seenState = -1;
    juce::uint32 sigCurve = 0, sigHist = 0, sigAccent = 0;

    juce::TooltipWindow tooltips { this, 450 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DuckwerkEditor)
};
