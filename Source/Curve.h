#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace dw
{
// One point of the drawn gain curve. x = position in the cycle (0..1),
// y = gain (0 = fully ducked, 1 = untouched), c = bend of the segment to the next point.
struct Node
{
    float x = 0.0f, y = 0.0f, c = 0.0f;
};

inline float shapeT (float t, float c)
{
    if (std::abs (c) < 0.001f)
        return t;
    const float k = c * 7.0f;
    return (std::exp (k * t) - 1.0f) / (std::exp (k) - 1.0f);
}

inline float evalCurve (const std::vector<Node>& n, float x)
{
    if (n.empty())
        return 1.0f;
    for (size_t i = 0; i + 1 < n.size(); ++i)
    {
        if (x < n[i + 1].x)
        {
            const float w = n[i + 1].x - n[i].x;
            const float t = w > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (x - n[i].x) / w) : 1.0f;
            return n[i].y + (n[i + 1].y - n[i].y) * shapeT (t, n[i].c);
        }
    }
    return n.back().y;
}

// Lookup table the audio thread reads; the UI thread re-renders it after every edit.
struct CurveTable
{
    static constexpr int size = 2048;
    std::array<std::atomic<float>, size> v;

    CurveTable()
    {
        for (auto& a : v)
            a.store (1.0f);
    }

    void render (const std::vector<Node>& nodes)
    {
        for (int i = 0; i < size; ++i)
            v[(size_t) i].store (evalCurve (nodes, (float) i / (float) (size - 1)), std::memory_order_relaxed);
    }

    float lookup (float p) const
    {
        const float f = juce::jlimit (0.0f, 1.0f, p) * (float) (size - 1);
        const int i = (int) f;
        const int j = juce::jmin (i + 1, size - 1);
        const float a = v[(size_t) i].load (std::memory_order_relaxed);
        const float b = v[(size_t) j].load (std::memory_order_relaxed);
        return a + (b - a) * (f - (float) i);
    }
};

struct Preset
{
    const char* name;
    std::vector<Node> nodes;
};

inline const std::vector<Preset>& presets()
{
    static const std::vector<Preset> p = {
        { "Classic Duck", { { 0.0f, 0.0f, 0.3f }, { 0.45f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
        { "Tight (Hard Techno)", { { 0.0f, 0.0f, 0.0f }, { 0.0625f, 0.0f, 0.2f }, { 0.28f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
        { "Long Pump", { { 0.0f, 0.0f, -0.35f }, { 0.875f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
        { "Hold + Release", { { 0.0f, 0.0f, 0.0f }, { 0.25f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
        { "Pre-Duck (click-free)", { { 0.0f, 0.0f, 0.3f }, { 0.375f, 1.0f, 0.0f }, { 0.9375f, 1.0f, 0.4f }, { 1.0f, 0.0f, 0.0f } } },
        { "Offbeat Gate", { { 0.0f, 0.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
        { "Double Pump", { { 0.0f, 0.0f, 0.2f }, { 0.25f, 1.0f, 0.0f }, { 0.5f, 0.35f, 0.2f }, { 0.75f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
        { "Soft (50%)", { { 0.0f, 0.5f, 0.2f }, { 0.5f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } } },
    };
    return p;
}

inline juce::String nodesToString (const std::vector<Node>& n)
{
    juce::String s;
    for (auto& p : n)
        s << juce::String (p.x, 5) << ":" << juce::String (p.y, 5) << ":" << juce::String (p.c, 4) << ";";
    return s;
}

inline std::vector<Node> nodesFromString (const juce::String& s)
{
    std::vector<Node> out;
    for (auto& tok : juce::StringArray::fromTokens (s, ";", ""))
    {
        auto parts = juce::StringArray::fromTokens (tok, ":", "");
        if (parts.size() == 3)
            out.push_back ({ juce::jlimit (0.0f, 1.0f, parts[0].getFloatValue()),
                             juce::jlimit (0.0f, 1.0f, parts[1].getFloatValue()),
                             juce::jlimit (-1.0f, 1.0f, parts[2].getFloatValue()) });
    }
    if (out.size() < 2)
        return presets().front().nodes;
    out.front().x = 0.0f;
    out.back().x = 1.0f;
    return out;
}
} // namespace dw
