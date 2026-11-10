#include "PluginEditor.h"

namespace col
{
const juce::Colour bg { 0xff0d1015 };
const juce::Colour panel { 0xff151a21 };
const juce::Colour panel2 { 0xff1e252f };
const juce::Colour line { 0xff2b3542 };
const juce::Colour text { 0xffd9dfe7 };
const juce::Colour dim { 0xff7d8997 };
const juce::Colour kick { 0xffff8a3d };
const juce::Colour bass { 0xff35c8f5 };
const juce::Colour clash { 0xffff3b6b };
const juce::Colour curve { 0xfff5f7fa };
const juce::Colour gain { 0xffb6f23c };
} // namespace col

namespace
{
juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

using V = dw::VisData;

juce::uint32 mix (juce::uint32 h, juce::uint32 v)
{
    return (h ^ v) * 16777619U;
}

int modeOf (DuckwerkProcessor& p)
{
    return (int) p.apvts.getRawParameterValue ("mode")->load();
}

constexpr int kW = 1100, kH = 616, kMargin = 16, kSlots = 14;
constexpr int kCurveY = 52, kCurveH = 272, kAccentY = 330, kAccentH = 40, kHistY = 376, kHistH = 96, kKnobY = 480, kKnobH = 128;

struct Group
{
    int first, count;
    const char* title;
};
const Group kGroups[] = { { 0, 2, "DUCK" }, { 2, 2, "BAND" }, { 4, 3, "KICK DETECTOR" }, { 7, 4, "HUMANIZE" }, { 11, 3, "SMALL SPEAKERS" } };
} // namespace

namespace dwui
{
//==============================================================================
Look::Look()
{
    using namespace juce;
    setColour (Slider::textBoxTextColourId, col::text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (Label::textColourId, col::dim);
    setColour (ComboBox::backgroundColourId, col::panel2);
    setColour (ComboBox::outlineColourId, col::line);
    setColour (ComboBox::textColourId, col::text);
    setColour (ComboBox::arrowColourId, col::dim);
    setColour (ComboBox::buttonColourId, col::panel2);
    setColour (PopupMenu::backgroundColourId, col::panel2);
    setColour (PopupMenu::textColourId, col::text);
    setColour (PopupMenu::highlightedBackgroundColourId, col::kick);
    setColour (PopupMenu::highlightedTextColourId, Colours::black);
    setColour (TextButton::buttonColourId, col::panel2);
    setColour (TextButton::buttonOnColourId, col::kick);
    setColour (TextButton::textColourOffId, col::text);
    setColour (TextButton::textColourOnId, Colours::black);
    setColour (TooltipWindow::backgroundColourId, Colour (0xff2a3340));
    setColour (TooltipWindow::textColourId, col::text);
    setColour (TooltipWindow::outlineColourId, col::kick.withAlpha (0.6f));
    setColour (AlertWindow::backgroundColourId, col::panel);
    setColour (AlertWindow::textColourId, col::text);
    setColour (AlertWindow::outlineColourId, col::line);
    setColour (TextEditor::backgroundColourId, col::panel2);
    setColour (TextEditor::textColourId, col::text);
    setColour (TextEditor::outlineColourId, col::line);
}

void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const float alpha = s.isEnabled() ? 1.0f : 0.3f;

    juce::Path track, value;
    track.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, a0, a1, true);
    value.addCentredArc (c.x, c.y, r - 3.0f, r - 3.0f, 0.0f, a0, ang, true);
    g.setColour (col::line.withMultipliedAlpha (alpha));
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::kick.withMultipliedAlpha (alpha));
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (col::panel2.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (2.0f * (r - 9.0f), 2.0f * (r - 9.0f)).withCentre (c));
    g.setColour (col::text.withMultipliedAlpha (alpha));
    const juce::Point<float> p1 (c.x + std::sin (ang) * (r - 18.0f), c.y - std::cos (ang) * (r - 18.0f));
    const juce::Point<float> p2 (c.x + std::sin (ang) * (r - 10.0f), c.y - std::cos (ang) * (r - 10.0f));
    g.drawLine ({ p1, p2 }, 2.0f);
}

//==============================================================================
CurveView::CurveView (DuckwerkProcessor& p) : proc (p)
{
    setOpaque (true);
    syncFromProcessor();
}

void CurveView::syncFromProcessor()
{
    const int v = proc.curveVersion.load();
    if (v != seenVersion && dragNode < 0 && dragHandle < 0)
    {
        seenVersion = v;
        nodes = proc.getNodes();
    }
}

juce::uint32 CurveView::signature() const
{
    auto& v = proc.vis;
    juce::uint32 h = 2166136261U;
    h = mix (h, (juce::uint32) (int) (v.phase.load() * 4000.0f));
    h = mix (h, (juce::uint32) v.writeCycle.load());
    h = mix (h, (juce::uint32) proc.curveVersion.load());
    h = mix (h, (juce::uint32) modeOf (proc));
    h = mix (h, (juce::uint32) proc.gridDiv.load());
    h = mix (h, v.scActive.load() ? 1U : 0U);
    h = mix (h, proc.apvts.getRawParameterValue ("check")->load() > 0.5f ? 1U : 0U);
    return h;
}

juce::Rectangle<float> CurveView::plot() const
{
    return getLocalBounds().toFloat().reduced (12.0f, 14.0f).withTrimmedBottom (14.0f);
}

juce::Point<float> CurveView::toPx (float x, float y) const
{
    const auto b = plot();
    return { b.getX() + x * b.getWidth(), b.getBottom() - y * b.getHeight() };
}

bool CurveView::handlePos (int seg, juce::Point<float>& out) const
{
    if (seg < 0 || seg + 1 >= (int) nodes.size())
        return false;
    const auto& a = nodes[(size_t) seg];
    const auto& b = nodes[(size_t) seg + 1];
    if (b.x - a.x < 0.03f || std::abs (b.y - a.y) < 0.02f)
        return false;
    out = toPx (0.5f * (a.x + b.x), a.y + (b.y - a.y) * dw::shapeT (0.5f, a.c));
    return true;
}

int CurveView::hitNode (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 10.0f;
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const float d = toPx (nodes[(size_t) i].x, nodes[(size_t) i].y).getDistanceFrom (p);
        if (d < bestD)
        {
            bestD = d;
            best = i;
        }
    }
    return best;
}

int CurveView::hitHandle (juce::Point<float> p) const
{
    for (int i = 0; i + 1 < (int) nodes.size(); ++i)
    {
        juce::Point<float> h;
        if (handlePos (i, h) && h.getDistanceFrom (p) < 9.0f)
            return i;
    }
    return -1;
}

float CurveView::snapX (float x, const juce::MouseEvent& e) const
{
    const int grid = proc.gridDiv.load();
    if (grid > 0 && ! e.mods.isShiftDown())
        x = std::round (x * (float) grid) / (float) grid;
    return juce::jlimit (0.0f, 1.0f, x);
}

void CurveView::commit()
{
    proc.setNodes (nodes);
    seenVersion = proc.curveVersion.load();
    if (onEdit)
        onEdit();
    repaint();
}

void CurveView::mouseDown (const juce::MouseEvent& e)
{
    if (modeOf (proc) == DuckwerkProcessor::Envelope)
        return;

    const auto p = e.position;
    const int n = hitNode (p);
    if (n >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            if (n > 0 && n < (int) nodes.size() - 1)
            {
                nodes.erase (nodes.begin() + n);
                commit();
            }
            return;
        }
        dragNode = n;
        return;
    }

    const int h = hitHandle (p);
    if (h >= 0)
    {
        dragHandle = h;
        dragStartC = nodes[(size_t) h].c;
        return;
    }

    if (e.mods.isPopupMenu())
        return;

    const auto b = plot();
    dw::Node nn;
    nn.x = snapX ((p.x - b.getX()) / b.getWidth(), e);
    nn.y = juce::jlimit (0.0f, 1.0f, (b.getBottom() - p.y) / b.getHeight());
    int idx = 1;
    while (idx < (int) nodes.size() - 1 && nodes[(size_t) idx].x <= nn.x)
        ++idx;
    nodes.insert (nodes.begin() + idx, nn);
    dragNode = idx;
    commit();
}

void CurveView::mouseDrag (const juce::MouseEvent& e)
{
    const auto b = plot();
    if (dragNode >= 0 && dragNode < (int) nodes.size())
    {
        auto& n = nodes[(size_t) dragNode];
        float y = juce::jlimit (0.0f, 1.0f, (b.getBottom() - e.position.y) / b.getHeight());
        if (y < 0.03f)
            y = 0.0f;
        if (y > 0.97f)
            y = 1.0f;
        n.y = y;
        if (dragNode > 0 && dragNode < (int) nodes.size() - 1)
        {
            const float x = snapX ((e.position.x - b.getX()) / b.getWidth(), e);
            n.x = juce::jlimit (nodes[(size_t) dragNode - 1].x, nodes[(size_t) dragNode + 1].x, x);
        }
        commit();
    }
    else if (dragHandle >= 0 && dragHandle + 1 < (int) nodes.size())
    {
        auto& a = nodes[(size_t) dragHandle];
        const float dir = nodes[(size_t) dragHandle + 1].y > a.y ? 1.0f : -1.0f;
        const float dy = (float) e.getDistanceFromDragStartY() / b.getHeight();
        a.c = juce::jlimit (-1.0f, 1.0f, dragStartC + dy * 2.5f * dir);
        commit();
    }
}

void CurveView::mouseUp (const juce::MouseEvent&)
{
    dragNode = dragHandle = -1;
}

void CurveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (modeOf (proc) == DuckwerkProcessor::Envelope)
        return;
    const int h = hitHandle (e.position);
    if (h >= 0 && hitNode (e.position) < 0)
    {
        nodes[(size_t) h].c = 0.0f;
        commit();
    }
}

juce::Path CurveView::layerPath (int cycle, int layer, int layer2) const
{
    auto& v = proc.vis;
    const auto b = plot();
    const float scale = 0.94f * b.getHeight() / norm;
    const float dx = b.getWidth() / (float) V::bins;
    juce::Path p;
    p.preallocateSpace (V::bins * 3 + 12);
    p.startNewSubPath (b.getX(), b.getBottom());
    for (int i = 0; i < V::bins; ++i)
    {
        float val = v.at (cycle, layer, i).load (std::memory_order_relaxed);
        if (layer2 >= 0)
            val = juce::jmin (val, v.at (cycle, layer2, i).load (std::memory_order_relaxed));
        p.lineTo (b.getX() + ((float) i + 0.5f) * dx, b.getBottom() - juce::jmin (b.getHeight(), val * scale));
    }
    p.lineTo (b.getRight(), b.getBottom());
    p.closeSubPath();
    return p;
}

juce::Path CurveView::gainPath (int cycle) const
{
    auto& v = proc.vis;
    const auto b = plot();
    const float dx = b.getWidth() / (float) V::bins;
    juce::Path p;
    bool open = false;
    for (int i = 0; i < V::bins; ++i)
    {
        const float gv = v.at (cycle, V::Gain, i).load (std::memory_order_relaxed);
        if (gv < 0.0f)
        {
            open = false;
            continue;
        }
        const juce::Point<float> pt (b.getX() + ((float) i + 0.5f) * dx, b.getBottom() - gv * b.getHeight());
        if (open)
            p.lineTo (pt);
        else
            p.startNewSubPath (pt);
        open = true;
    }
    return p;
}

// Background, grid, legend and all finished cycles. Runs once per cycle, not once per frame.
void CurveView::renderCache (float scale)
{
    auto& v = proc.vis;
    const int w = v.writeCycle.load();
    cachedCycle = w;
    cachedGrid = proc.gridDiv.load();
    cachedScale = scale;
    cacheDirty = false;

    const int iw = juce::jmax (1, juce::roundToInt ((float) getWidth() * scale));
    const int ih = juce::jmax (1, juce::roundToInt ((float) getHeight() * scale));
    if (cache.getWidth() != iw || cache.getHeight() != ih)
        cache = juce::Image (juce::Image::RGB, iw, ih, false);

    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.fillAll (col::bg);
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const auto b = plot();

    const int grid = juce::jmax (4, cachedGrid);
    for (int i = 0; i <= grid; ++i)
    {
        const bool major = (i * 4) % grid == 0;
        g.setColour (col::line.withAlpha (major ? 0.9f : 0.35f));
        g.fillRect (b.getX() + b.getWidth() * (float) i / (float) grid, b.getY(), 1.0f, b.getHeight());
    }
    g.setColour (col::line.withAlpha (0.5f));
    for (int i = 0; i <= 4; ++i)
        g.fillRect (b.getX(), b.getY() + b.getHeight() * (float) i / 4.0f, b.getWidth(), 1.0f);

    float peak = 0.0f;
    for (int c = 0; c < V::cycles; ++c)
        for (int i = 0; i < V::bins; ++i)
            peak = juce::jmax (peak, v.at (c, V::Kick, i).load (std::memory_order_relaxed), v.at (c, V::Pre, i).load (std::memory_order_relaxed));
    norm = juce::jmax (peak, norm * 0.8f, 0.02f);

    // Older cycles first, so repeated shapes build up and deviations stay faint.
    for (int age = V::cycles - 1; age >= 1; --age)
    {
        const int c = (w - age + V::cycles) % V::cycles;
        const float a = 0.34f * std::pow (0.74f, (float) age);
        g.setColour (col::kick.withAlpha (a));
        g.fillPath (layerPath (c, V::Kick));
        g.setColour (col::bass.withAlpha (a));
        g.fillPath (layerPath (c, V::Post));
    }

    // Last complete cycle: the bass before ducking, and where the lows still collide.
    const int last = (w - 1 + V::cycles) % V::cycles;
    g.setColour (col::bass.withAlpha (0.45f));
    g.strokePath (layerPath (last, V::Pre), juce::PathStrokeType (1.0f));
    g.setColour (col::clash.withAlpha (0.6f));
    g.fillPath (layerPath (last, V::KickLow, V::PostLow));
    g.setColour (col::gain.withAlpha (0.35f));
    g.strokePath (gainPath (last), juce::PathStrokeType (1.4f));

    float sumKick = 0.0f, sumClash = 0.0f;
    for (int i = 0; i < V::bins; ++i)
    {
        const float k = v.at (last, V::KickLow, i).load (std::memory_order_relaxed);
        sumKick += k;
        sumClash += juce::jmin (k, v.at (last, V::PostLow, i).load (std::memory_order_relaxed));
    }
    clashPct = sumKick > 1.0e-4f ? juce::roundToInt (100.0f * sumClash / sumKick) : -1;

    g.setFont (font (12.0f));
    float lx = b.getX() + 8.0f;
    auto legend = [&] (juce::Colour c, const juce::String& t)
    {
        g.setColour (c);
        g.fillRoundedRectangle (lx, b.getY() + 9.0f, 10.0f, 10.0f, 2.0f);
        g.setColour (col::text.withAlpha (0.85f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t) + 6.0f;
        g.drawText (t, juce::Rectangle<float> (lx + 15.0f, b.getY() + 5.0f, tw, 18.0f), juce::Justification::centredLeft);
        lx += tw + 28.0f;
    };
    legend (col::kick, "Kick");
    legend (col::bass, "Bass");
    legend (col::clash, "Low-end clash");
    legend (col::gain, "Applied gain");
    legend (col::curve, "Your curve");
}

void CurveView::paint (juce::Graphics& g)
{
    auto& v = proc.vis;
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int w = v.writeCycle.load();
    if (cacheDirty || w != cachedCycle || proc.gridDiv.load() != cachedGrid || std::abs (scale - cachedScale) > 0.01f)
        renderCache (scale);
    g.drawImage (cache, getLocalBounds().toFloat());

    const auto b = plot();
    const int mode = modeOf (proc);
    const bool envMode = mode == DuckwerkProcessor::Envelope;

    // The cycle being recorded right now
    g.setColour (col::kick.withAlpha (0.34f));
    g.fillPath (layerPath (w, V::Kick));
    g.setColour (col::bass.withAlpha (0.34f));
    g.fillPath (layerPath (w, V::Post));
    g.setColour (col::gain.withAlpha (0.9f));
    g.strokePath (gainPath (w), juce::PathStrokeType (1.4f));

    if (clashPct >= 0)
    {
        g.setFont (font (12.0f));
        g.setColour (clashPct > 35 ? col::clash : col::dim);
        g.drawText ("Low-end clash: " + juce::String (clashPct) + " %", b.reduced (8.0f, 6.0f), juce::Justification::topRight);
    }

    const float ph = v.phase.load();
    if (ph >= 0.0f)
    {
        g.setColour (col::text.withAlpha (0.5f));
        g.fillRect (b.getX() + ph * b.getWidth(), b.getY(), 1.0f, b.getHeight());
    }

    // Drawn curve
    juce::Path p;
    for (int i = 0; i <= 400; ++i)
    {
        const float x = (float) i / 400.0f;
        const auto pt = toPx (x, dw::evalCurve (nodes, x));
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    g.setColour (col::curve.withAlpha (envMode ? 0.18f : 0.95f));
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (! envMode)
    {
        for (int i = 0; i + 1 < (int) nodes.size(); ++i)
        {
            juce::Point<float> h;
            if (handlePos (i, h))
            {
                g.setColour (col::curve.withAlpha (0.6f));
                g.drawEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (h), 1.3f);
            }
        }
        for (auto& n : nodes)
        {
            const auto c = toPx (n.x, n.y);
            g.setColour (col::bg);
            g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c));
            g.setColour (col::curve);
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
        }
    }

    // One status line: the most important thing to know right now.
    g.setColour (col::dim);
    g.setFont (font (11.5f));
    juce::String hint = envMode ? "Envelope mode: ducking follows the kick level, the curve is not used."
                                : "Top = full volume, bottom = silent. Click to add a point, drag to move.";
    if (mode != DuckwerkProcessor::Sync && ! v.scActive.load())
    {
        g.setColour (col::clash);
        hint = "No kick signal. Route the kick to this plugin's sidechain input (press ? for steps).";
    }
    if (proc.apvts.getRawParameterValue ("check")->load() > 0.5f)
    {
        g.setColour (col::clash);
        hint = "PREVIEW is on: sub bass is muted. Turn it off before you export!";
    }
    g.drawText (hint, juce::Rectangle<float> (b.getX(), b.getBottom() + 5.0f, b.getWidth(), 18.0f), juce::Justification::centredRight);
}

//==============================================================================
juce::uint32 HistoryView::signature() const
{
    juce::uint32 h = 2166136261U;
    h = mix (h, (juce::uint32) proc.vis.histWrite.load());
    h = mix (h, (juce::uint32) proc.histBeats.load());
    h = mix (h, proc.vis.freeze.load() ? 1U : 0U);
    return h;
}

void HistoryView::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.fillAll (col::bg);
    g.setColour (col::panel);
    g.fillRoundedRectangle (full, 6.0f);
    const auto b = full.reduced (12.0f, 6.0f);
    auto& v = proc.vis;
    const int w = v.histWrite.load();

    // One point per horizontal pixel is all the screen can show.
    const int n = juce::jlimit (64, V::cols, (int) b.getWidth());
    const float colsPerPoint = (float) V::cols / (float) n;
    std::array<float, V::cols> kick, pre, post, gn;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const int c0 = (int) ((float) i * colsPerPoint);
        const int c1 = juce::jmax (c0 + 1, (int) ((float) (i + 1) * colsPerPoint));
        float k = 0.0f, pr = 0.0f, po = 0.0f, gg = 1.0f, flag = 0.0f;
        for (int c = c0; c < c1 && c < V::cols; ++c)
        {
            const int idx = (w + c) % V::cols;
            k = juce::jmax (k, v.h (V::HKick, idx).load (std::memory_order_relaxed));
            pr = juce::jmax (pr, v.h (V::HPre, idx).load (std::memory_order_relaxed));
            po = juce::jmax (po, v.h (V::HPost, idx).load (std::memory_order_relaxed));
            gg = juce::jmin (gg, v.h (V::HGain, idx).load (std::memory_order_relaxed));
            flag = juce::jmax (flag, v.h (V::HFlag, idx).load (std::memory_order_relaxed));
        }
        kick[(size_t) i] = k;
        pre[(size_t) i] = pr;
        post[(size_t) i] = po;
        gn[(size_t) i] = gg;
        peak = juce::jmax (peak, k, pr);
        if (flag > 0.5f)
        {
            g.setColour (col::line.withAlpha (flag > 1.5f ? 1.0f : 0.45f));
            g.fillRect (b.getX() + b.getWidth() * (float) i / (float) (n - 1), b.getY(), 1.0f, b.getHeight());
        }
    }
    norm = juce::jmax (peak, norm * 0.97f, 0.02f);

    const float cy = b.getCentreY();
    const float hh = b.getHeight() * 0.48f / norm;
    const float dx = b.getWidth() / (float) (n - 1);
    const float maxH = b.getHeight() * 0.5f;

    auto mirrored = [&] (const std::array<float, V::cols>& d)
    {
        juce::Path p;
        p.preallocateSpace (n * 6 + 12);
        p.startNewSubPath (b.getX(), cy);
        for (int i = 0; i < n; ++i)
            p.lineTo (b.getX() + (float) i * dx, cy - juce::jmin (maxH, d[(size_t) i] * hh));
        for (int i = n - 1; i >= 0; --i)
            p.lineTo (b.getX() + (float) i * dx, cy + juce::jmin (maxH, d[(size_t) i] * hh));
        p.closeSubPath();
        return p;
    };

    g.setColour (col::bass.withAlpha (0.18f));
    g.fillPath (mirrored (pre));
    g.setColour (col::kick.withAlpha (0.75f));
    g.fillPath (mirrored (kick));
    g.setColour (col::bass.withAlpha (0.6f));
    g.fillPath (mirrored (post));

    juce::Path gp;
    for (int i = 0; i < n; ++i)
    {
        const juce::Point<float> pt (b.getX() + (float) i * dx, b.getBottom() - gn[(size_t) i] * b.getHeight());
        if (i == 0)
            gp.startNewSubPath (pt);
        else
            gp.lineTo (pt);
    }
    g.setColour (col::gain.withAlpha (0.8f));
    g.strokePath (gp, juce::PathStrokeType (1.2f));

    g.setColour (col::dim);
    g.setFont (font (11.5f, true));
    g.drawText (v.freeze.load() ? "HISTORY (frozen)" : "HISTORY", b.reduced (4.0f, 0.0f), juce::Justification::bottomLeft);
}

//==============================================================================
juce::Rectangle<float> AccentView::cell (int i) const
{
    const auto b = getLocalBounds().toFloat().reduced (12.0f, 5.0f).withTrimmedLeft (86.0f);
    const float w = b.getWidth() / (float) DuckwerkProcessor::numAccents;
    return juce::Rectangle<float> (b.getX() + (float) i * w, b.getY(), w, b.getHeight()).reduced (2.0f, 0.0f);
}

juce::uint32 AccentView::signature() const
{
    juce::uint32 h = 2166136261U;
    for (auto& a : proc.accents)
        h = mix (h, (juce::uint32) (a.load() * 1000.0f));
    h = mix (h, (juce::uint32) proc.vis.step.load());
    h = mix (h, proc.vis.phase.load() >= 0.0f ? 1U : 0U);
    h = mix (h, (juce::uint32) modeOf (proc));
    return h;
}

void AccentView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (col::dim);
    g.setFont (font (11.5f, true));
    g.drawText ("ACCENT", getLocalBounds().toFloat().reduced (12.0f, 0.0f).removeFromLeft (80.0f), juce::Justification::centredLeft);

    const bool active = modeOf (proc) != DuckwerkProcessor::Envelope;
    const int step = proc.vis.step.load();
    const bool running = proc.vis.phase.load() >= 0.0f;
    for (int i = 0; i < DuckwerkProcessor::numAccents; ++i)
    {
        const auto c = cell (i);
        const float val = proc.accents[(size_t) i].load();
        g.setColour (col::panel2);
        g.fillRoundedRectangle (c, 3.0f);
        const bool cur = active && running && i == step;
        g.setColour ((cur ? col::kick : col::kick.withAlpha (0.45f)).withMultipliedAlpha (active ? 1.0f : 0.3f));
        g.fillRoundedRectangle (c.withTrimmedTop (c.getHeight() * (1.0f - val)), 3.0f);
        g.setColour (col::text.withAlpha (active ? 0.9f : 0.3f));
        g.setFont (font (11.0f));
        g.drawText ("Hit " + juce::String (i + 1) + ":  " + juce::String (juce::roundToInt (val * 100.0f)) + "%", c, juce::Justification::centred);
    }
}

void AccentView::edit (const juce::MouseEvent& e)
{
    for (int i = 0; i < DuckwerkProcessor::numAccents; ++i)
    {
        const auto c = cell (i).expanded (2.0f, 0.0f);
        if (e.position.x >= c.getX() && e.position.x < c.getRight())
        {
            float val = juce::jlimit (0.0f, 1.0f, (c.getBottom() - e.position.y) / c.getHeight());
            val = std::round (val * 20.0f) / 20.0f;
            proc.accents[(size_t) i].store (val);
            repaint();
        }
    }
}

void AccentView::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int i = 0; i < DuckwerkProcessor::numAccents; ++i)
        if (cell (i).expanded (2.0f, 6.0f).contains (e.position))
            proc.accents[(size_t) i].store (1.0f);
    repaint();
}

//==============================================================================
void HelpOverlay::paint (juce::Graphics& g)
{
    const auto full = getLocalBounds().toFloat();
    g.setColour (col::bg.withAlpha (0.96f));
    g.fillRoundedRectangle (full, 6.0f);
    g.setColour (col::kick.withAlpha (0.6f));
    g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);

    auto area = getLocalBounds().reduced (34, 22);
    g.setColour (col::text);
    g.setFont (font (18.0f, true));
    g.drawText ("QUICK START", area.removeFromTop (30), juce::Justification::centredLeft);
    area.removeFromTop (4);

    struct Line
    {
        const char* head;
        const char* body;
    };
    const Line lines[] = {
        { "1  Load it on the bass", "Put Duckwerk on the mixer track of your BASS (or anything that should make room for the kick)." },
        { "2  SYNC mode: no routing", "Works at once while the song plays. Set LENGTH to your kick spacing (1 Beat = 4-on-the-floor) and draw the curve." },
        { "3  TRIGGER / ENVELOPE: route the kick", "Select the KICK track, right-click the small arrow at the bottom of the BASS track, choose \"Sidechain to this track\". "
                                                   "Then open the plugin's gear icon > Processing and set the sidechain input to 1." },
        { "4  Read the picture", "Orange = kick, blue = bass, red = both fighting for the low end. Red is big? Duck deeper or longer." },
        { "5  Sounds robotic?", "Lower a few ACCENT bars, or turn up Var Depth / Var Shape. The variation repeats, so the groove keeps its structure." },
        { "6  No bass on small speakers?", "Turn up Harmonics. PREVIEW lets you hear the mix like a small speaker would." },
    };
    for (auto& l : lines)
    {
        auto row = area.removeFromTop (54);
        g.setColour (col::kick);
        g.setFont (font (13.5f, true));
        g.drawText (l.head, row.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (col::text.withAlpha (0.85f));
        g.setFont (font (13.0f));
        g.drawFittedText (l.body, row, juce::Justification::topLeft, 2);
    }
    g.setColour (col::dim);
    g.setFont (font (12.0f));
    g.drawText ("Hover any control for a short tip.  Click anywhere to close.", getLocalBounds().reduced (34, 14), juce::Justification::bottomLeft);
}
} // namespace dwui

//==============================================================================
DuckwerkEditor::DuckwerkEditor (DuckwerkProcessor& p)
    : AudioProcessorEditor (&p), proc (p), curveView (p), accentView (p), historyView (p)
{
    setLookAndFeel (&look);
    setOpaque (true);
    addAndMakeVisible (curveView);
    addAndMakeVisible (accentView);
    addAndMakeVisible (historyView);

    curveView.setTooltip ("Your ducking curve over one cycle. Top = full volume, bottom = silent.\n"
                          "Click: add point.  Right-click: remove point.  Drag a ring: bend the line.  Hold Shift: ignore the grid.");
    accentView.setTooltip ("How deep each hit ducks. The 8 hits repeat. Drag a bar down so some hits duck less - this breaks the machine-like feel. Double-click: reset.");
    historyView.setTooltip ("The last bars. Orange = kick, blue = bass after ducking, pale blue = bass before, green line = volume applied to the bass.");

    // Mode
    const char* modeNames[] = { "SYNC", "TRIGGER", "ENVELOPE" };
    const char* modeTips[] = {
        "Curve repeats in time with the song tempo. Needs no routing. Best for a steady 4-on-the-floor kick.",
        "Every real kick hit restarts the curve. Best for off-grid kicks, rolls and breaks. Needs the kick on the sidechain input (see ?).",
        "No curve: the bass gets quieter the louder the kick is, like a classic sidechain compressor. Needs the kick on the sidechain input (see ?)."
    };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeButtons[i];
        b.setButtonText (modeNames[i]);
        b.setTooltip (modeTips[i]);
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
        b.onClick = [this, i] { modeAtt->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    modeAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("mode"), [this] (float v)
    {
        for (int i = 0; i < 3; ++i)
            modeButtons[i].setToggleState (juce::roundToInt (v) == i, juce::dontSendNotification);
    });
    modeAtt->sendInitialUpdate();

    lengthBox.addItemList (DuckwerkProcessor::lengthNames(), 1);
    lengthBox.setTooltip ("How long one pass through the curve lasts. Set it to the distance between your kicks: 1 Beat for a kick on every beat. "
                          "3/4 or 1.5 Beats run against the bar for a polyrhythmic pump.");
    addAndMakeVisible (lengthBox);
    lengthAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "length", lengthBox);

    int id = 1;
    for (auto& pr : dw::presets())
        shapeBox.addItem (juce::String::fromUTF8 (pr.name), id++);
    shapeBox.setTextWhenNothingSelected ("Custom");
    shapeBox.setTooltip ("Ready-made curves as a starting point. Pick one, then edit it by hand.");
    shapeBox.onChange = [this]
    {
        const int sel = shapeBox.getSelectedId();
        if (sel > 0)
        {
            proc.setNodes (dw::presets()[(size_t) sel - 1].nodes);
            curveView.syncFromProcessor();
            curveView.repaint();
        }
    };
    addAndMakeVisible (shapeBox);
    curveView.onEdit = [this] { shapeBox.setSelectedId (0, juce::dontSendNotification); };

    presetBox.setTooltip ("Your saved presets (all settings, curve and accents). Loading one replaces the current settings.");
    presetBox.onChange = [this] { presetChosen(); };
    addAndMakeVisible (presetBox);
    refreshPresetList();

    saveButton.setTooltip ("Save all current settings as a preset. Using an existing name overwrites it.");
    saveButton.onClick = [this] { askSavePreset(); };
    addAndMakeVisible (saveButton);

    helpButton.setTooltip ("Quick start, including how to route the kick into the sidechain.");
    helpButton.onClick = [this]
    {
        help.setVisible (! help.isVisible());
        help.toFront (false);
    };
    addAndMakeVisible (helpButton);

    for (int gdiv : { 4, 8, 12, 16, 24, 32 })
        gridBox.addItem ("Grid 1/" + juce::String (gdiv), gdiv);
    gridBox.setSelectedId (proc.gridDiv.load(), juce::dontSendNotification);
    gridBox.setTooltip ("Points snap to these steps of the cycle. 1/12 and 1/24 give triplets. Hold Shift while dragging to place freely.");
    gridBox.onChange = [this] { proc.gridDiv.store (gridBox.getSelectedId()); };
    addAndMakeVisible (gridBox);

    for (int bars : { 1, 2, 4, 8, 16 })
        histBox.addItem (juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars * 4);
    histBox.setSelectedId (proc.histBeats.load(), juce::dontSendNotification);
    histBox.setTooltip ("How much time the history strip shows.");
    histBox.onChange = [this] { proc.histBeats.store (juce::jmax (4, histBox.getSelectedId())); };
    addAndMakeVisible (histBox);

    freezeButton.setClickingTogglesState (true);
    freezeButton.setToggleState (proc.vis.freeze.load(), juce::dontSendNotification);
    freezeButton.setTooltip ("Hold the display still so you can look at it. The sound is not affected.");
    freezeButton.onClick = [this] { proc.vis.freeze.store (freezeButton.getToggleState()); };
    addAndMakeVisible (freezeButton);

    addKnob (depth, "depth", "Depth", " %", "How much the bass is turned down. 100 % = silent at the lowest point of the curve.");
    addKnob (smooth, "smooth", "Smooth", " ms", "Rounds off sudden volume jumps to avoid clicks. Raise it if you hear clicks, lower it for a tighter duck.");
    addKnob (xover, "xover", "Split at", " Hz", "With LOW ONLY on: only sound below this frequency is ducked.");
    addKnob (thresh, "thresh", "Threshold", " dB", "How loud the kick must be to count. Trigger mode: lower it if kicks are missed, raise it if other sounds trigger. "
                                                "Envelope mode: ducking starts here.");
    addKnob (hold, "hold", "Hold", " ms", "Trigger mode: time after a hit in which no new hit is accepted. Raise it if one kick triggers twice.");
    addKnob (release, "release", "Release", " ms", "Envelope mode: how fast the bass comes back after the kick.");
    addKnob (varDepth, "vardepth", "Var Depth", " %", "Each hit ducks a little differently deep. Makes the pump less machine-like.");
    addKnob (varShape, "varshape", "Var Shape", " %", "Each hit lets the bass back in a little earlier or later. Makes the groove breathe.");
    addKnob (seed, "seed", "Pattern", "", "Picks a different variation pattern. Turn until you like the groove - it always plays back the same.");
    addKnob (harm, "harm", "Harmonics", " %", "Adds higher tones made from your sub bass, so the bass stays audible on phones, laptops and small speakers. Start around 20-40 %.");
    addKnob (harmFreq, "harmfreq", "Below", " Hz", "Harmonics are made from everything below this frequency. Set it just above your lowest bass notes.");

    lowOnlyButton.setClickingTogglesState (true);
    lowOnlyButton.setTooltip ("On: only the low part of the bass is ducked, its upper tones keep playing through the kick. Off: the whole sound is ducked.");
    addAndMakeVisible (lowOnlyButton);
    lowOnlyAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "lowonly", lowOnlyButton);

    checkButton.setClickingTogglesState (true);
    checkButton.setColour (juce::TextButton::buttonOnColourId, col::clash);
    checkButton.setTooltip ("Listen like a small speaker: removes everything below 160 Hz. Only for checking - turn it off before you export!");
    addAndMakeVisible (checkButton);
    checkAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "check", checkButton);

    loopBox.addItemList ({ "4", "8", "16", "32", "Off" }, 1);
    loopBox.setTooltip ("The variation repeats after this many hits, so it sounds like a groove instead of random noise.");
    addAndMakeVisible (loopBox);
    loopAtt = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "loop", loopBox);
    loopLabel.setText ("Repeat (hits)", juce::dontSendNotification);
    loopLabel.setJustificationType (juce::Justification::centred);
    loopLabel.setFont (font (11.5f));
    addAndMakeVisible (loopLabel);

    addChildComponent (help);

    setSize (kW, kH);
    proc.editorOpen.store (true);
    timerCallback();
    startTimerHz (30);
}

DuckwerkEditor::~DuckwerkEditor()
{
    stopTimer();
    proc.editorOpen.store (false);
    setLookAndFeel (nullptr);
}

void DuckwerkEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name, const juce::String& suffix, const juce::String& tip)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setTooltip (tip);
    addAndMakeVisible (k.slider);
    k.att = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, k.slider);
    k.slider.setTextValueSuffix (suffix);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (font (11.5f));
    k.label.setTooltip (tip);
    addAndMakeVisible (k.label);
}

void DuckwerkEditor::refreshPresetList()
{
    presetFiles = DuckwerkProcessor::presetFolder().findChildFiles (juce::File::findFiles, false, "*.duckwerk");
    presetFiles.sort();
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < presetFiles.size(); ++i)
        presetBox.addItem (presetFiles[i].getFileNameWithoutExtension(), i + 1);
    if (! presetFiles.isEmpty())
        presetBox.addSeparator();
    presetBox.addItem ("Reset to default", 9001);
    presetBox.addItem ("Open preset folder...", 9002);

    const auto name = proc.getPresetName();
    presetBox.setText (name.isEmpty() ? "No preset" : name, juce::dontSendNotification);
}

void DuckwerkEditor::presetChosen()
{
    const int sel = presetBox.getSelectedId();
    if (sel >= 1 && sel <= presetFiles.size())
        proc.loadPreset (presetFiles[sel - 1]);
    else if (sel == 9001)
        proc.resetToDefault();
    else if (sel == 9002)
        DuckwerkProcessor::presetFolder().revealToUser();
    else
        return;
    refreshPresetList();
}

void DuckwerkEditor::askSavePreset()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name for this preset:", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&look);
    w->addTextEditor ("name", proc.getPresetName(), {});
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DuckwerkEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        const auto name = w->getTextEditorContents ("name");
        w->setLookAndFeel (nullptr);
        if (safe != nullptr && result == 1 && safe->proc.savePreset (name))
            safe->refreshPresetList();
    }), true);
}

juce::Rectangle<int> DuckwerkEditor::slot (int i) const
{
    const float w = (float) (kW - 2 * kMargin) / (float) kSlots;
    const int x0 = kMargin + juce::roundToInt ((float) i * w);
    const int x1 = kMargin + juce::roundToInt ((float) (i + 1) * w);
    return { x0, kKnobY, x1 - x0, kKnobH };
}

void DuckwerkEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::text);
    g.setFont (font (19.0f, true));
    g.drawText ("DUCKWERK", kMargin, 12, 130, 32, juce::Justification::centredLeft);

    g.setColour (col::dim);
    g.setFont (font (10.0f, true));
    auto caption = [&] (const char* t, const juce::Component& c)
    {
        g.drawText (t, c.getX() + 2, 3, 160, 13, juce::Justification::centredLeft);
    };
    caption ("MODE", modeButtons[0]);
    caption ("CURVE LENGTH", lengthBox);
    caption ("CURVE SHAPE", shapeBox);
    caption ("PRESET", presetBox);

    for (auto& gr : kGroups)
    {
        const auto r = slot (gr.first).getUnion (slot (gr.first + gr.count - 1)).reduced (3, 0).toFloat();
        g.setColour (col::panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col::dim);
        g.setFont (font (11.0f, true));
        g.drawText (gr.title, r.withHeight (22.0f).reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void DuckwerkEditor::resized()
{
    const int inner = kW - 2 * kMargin;
    const int y = 18, h = 26;
    int x = kMargin + 136;
    for (auto& b : modeButtons)
    {
        b.setBounds (x, y, 84, h);
        x += 84;
    }
    x += 14;
    lengthBox.setBounds (x, y, 146, h);
    x += 156;
    shapeBox.setBounds (x, y, 176, h);
    x += 200;
    presetBox.setBounds (x, y, 196, h);
    x += 202;
    saveButton.setBounds (x, y, 60, h);
    helpButton.setBounds (kW - kMargin - 32, y, 32, h);

    curveView.setBounds (kMargin, kCurveY, inner, kCurveH);
    accentView.setBounds (kMargin, kAccentY, inner, kAccentH);
    historyView.setBounds (kMargin, kHistY, inner, kHistH);
    help.setBounds (kMargin, kCurveY, inner, kHistY + kHistH - kCurveY);

    gridBox.setBounds (kMargin + 12, kCurveY + kCurveH - 25, 96, 20);
    freezeButton.setBounds (kW - kMargin - 12 - 66, kHistY + 6, 66, 22);
    histBox.setBounds (freezeButton.getX() - 6 - 84, kHistY + 6, 84, 22);

    auto place = [this] (Knob& k, int i)
    {
        const auto s = slot (i);
        k.slider.setBounds (s.getX() + 4, s.getY() + 22, s.getWidth() - 8, 82);
        k.label.setBounds (s.getX(), s.getY() + 106, s.getWidth(), 16);
    };
    place (depth, 0);
    place (smooth, 1);
    lowOnlyButton.setBounds (slot (2).reduced (8, 0).withY (kKnobY + 48).withHeight (28));
    place (xover, 3);
    place (thresh, 4);
    place (hold, 5);
    place (release, 6);
    place (varDepth, 7);
    place (varShape, 8);
    loopBox.setBounds (slot (9).reduced (4, 0).withY (kKnobY + 48).withHeight (26));
    loopLabel.setBounds (slot (9).withY (kKnobY + 106).withHeight (16));
    place (seed, 10);
    place (harm, 11);
    place (harmFreq, 12);
    checkButton.setBounds (slot (13).reduced (6, 0).withY (kKnobY + 48).withHeight (28));
}

void DuckwerkEditor::timerCallback()
{
    const int mode = modeOf (proc);
    const bool sync = mode == DuckwerkProcessor::Sync;
    const bool trig = mode == DuckwerkProcessor::Trigger;
    const bool env = mode == DuckwerkProcessor::Envelope;

    auto enable = [] (Knob& k, bool on)
    {
        k.slider.setEnabled (on);
        k.label.setAlpha (on ? 1.0f : 0.35f);
    };
    enable (thresh, ! sync);
    enable (hold, trig);
    enable (release, env);
    enable (varDepth, ! env);
    enable (varShape, ! env);
    enable (seed, ! env);
    loopBox.setEnabled (! env);
    enable (xover, lowOnlyButton.getToggleState());
    shapeBox.setEnabled (! env);

    // A preset or the host loaded new settings: bring the non-parameter controls in line.
    if (const int sv = proc.stateVersion.load(); sv != seenState)
    {
        seenState = sv;
        gridBox.setSelectedId (proc.gridDiv.load(), juce::dontSendNotification);
        histBox.setSelectedId (proc.histBeats.load(), juce::dontSendNotification);
        shapeBox.setSelectedId (0, juce::dontSendNotification);
        const auto name = proc.getPresetName();
        presetBox.setText (name.isEmpty() ? "No preset" : name, juce::dontSendNotification);
    }

    curveView.syncFromProcessor();

    // Repaint only what actually changed since the last tick.
    if (const auto s = curveView.signature(); s != sigCurve)
    {
        sigCurve = s;
        curveView.repaint();
    }
    if (const auto s = historyView.signature(); s != sigHist)
    {
        sigHist = s;
        historyView.repaint();
    }
    if (const auto s = accentView.signature(); s != sigAccent)
    {
        sigAccent = s;
        accentView.repaint();
    }
}
