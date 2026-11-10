#pragma once
#include <array>
#include <atomic>

namespace dw
{
// Data the audio thread records for the display. Only the audio thread writes,
// the editor only reads, so relaxed atomics are enough.
struct VisData
{
    // Per-cycle memory: the last `cycles` passes through the curve, binned by phase.
    static constexpr int bins = 320;
    static constexpr int cycles = 10;
    enum Layer { Kick, Pre, Post, KickLow, PostLow, Gain, numLayers };

    // Scrolling history, one column per slice of time.
    static constexpr int cols = 1024;
    enum HLayer { HKick, HPre, HPost, HGain, HFlag, numHLayers };

    std::array<std::atomic<float>, (size_t) cycles * numLayers * bins> cyc;
    std::array<std::atomic<float>, (size_t) cols * numHLayers> hist;

    std::atomic<int> writeCycle { 0 };
    std::atomic<int> histWrite { 0 };
    std::atomic<float> phase { -1.0f };
    std::atomic<int> step { 0 };
    std::atomic<bool> scActive { false };
    std::atomic<bool> freeze { false };

    VisData()
    {
        for (int c = 0; c < cycles; ++c)
            clearCycle (c);
        for (int i = 0; i < cols; ++i)
        {
            for (int l = 0; l < numHLayers; ++l)
                h (l, i).store (0.0f);
            h (HGain, i).store (1.0f);
        }
    }

    std::atomic<float>& at (int cycle, int layer, int bin) { return cyc[(size_t) ((cycle * numLayers + layer) * bins + bin)]; }
    std::atomic<float>& h (int layer, int col) { return hist[(size_t) (col * numHLayers + layer)]; }

    void clearCycle (int c)
    {
        for (int l = 0; l < numLayers; ++l)
            for (int b = 0; b < bins; ++b)
                at (c, l, b).store (l == Gain ? -1.0f : 0.0f, std::memory_order_relaxed);
    }
};
} // namespace dw
