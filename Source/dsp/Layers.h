#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace samsi
{

//==============================================================================
/** Geladenes Kick-Sample (Mono). Unveränderlich, sobald veröffentlicht. */
struct KickSample
{
    std::vector<float> data;
    double rate = 48000.0;
    uint64_t id = 0;              // Inhalts-Hash (für "Analyse veraltet")
    float pitchHz = 0.0f;         // erkannter Grundton (0 = unbekannt)
    float pitchConfidence = 0.0f; // 0..1
    std::string name;
};

//==============================================================================
/** Bandbegrenzte Wavetable mit Mip-Map-Stufen.
    Jede Stufe enthält nur Obertöne bis kMaxHarm[L]; die Stufe wird nach Notenfrequenz
    gewählt, sodass keine Obertöne über ~0,45 x Samplerate entstehen (kein Aliasing).
    Unveränderlich, sobald veröffentlicht. */
struct WavetableData
{
    static constexpr int kNumLevels = 9;
    static constexpr std::array<int, kNumLevels> kMaxHarm { 400, 200, 100, 50, 25, 12, 6, 3, 1 };

    int numFrames = 0;
    std::array<int, kNumLevels> size {}, offset {};
    int frameStride = 0;
    std::vector<float> data;
    uint64_t id = 0;
    std::string name;

    void init (int frames)
    {
        numFrames = frames;
        int off = 0;
        for (int L = 0; L < kNumLevels; ++L)
        {
            int n = 64;
            while (n < kMaxHarm[(size_t) L] * 5) n *= 2;
            size[(size_t) L] = n;
            offset[(size_t) L] = off;
            off += n + 1;                 // +1 Schutzwert für Interpolation
        }
        frameStride = off;
        data.assign ((size_t) frameStride * (size_t) frames, 0.0f);
    }

    float* level (int frame, int L) noexcept       { return data.data() + (size_t) frame * (size_t) frameStride + (size_t) offset[(size_t) L]; }
    const float* level (int frame, int L) const noexcept { return data.data() + (size_t) frame * (size_t) frameStride + (size_t) offset[(size_t) L]; }

    /** Mip-Stufe für eine Grundfrequenz: höchste Obertonzahl, die unter 0,45 x sr bleibt. */
    static int levelFor (double freqHz, double sampleRate) noexcept
    {
        const double allowed = 0.45 * sampleRate / std::max (1.0, freqHz);
        for (int L = 0; L < kNumLevels; ++L)
            if ((double) kMaxHarm[(size_t) L] <= allowed) return L;
        return kNumLevels - 1;
    }

    /** pos01: Morph-Position über die Frames, phase01: Oszillatorphase. */
    float read (double pos01, double phase01, int L) const noexcept
    {
        if (numFrames <= 0) return 0.0f;
        const double fp = std::min (1.0, std::max (0.0, pos01)) * (double) (numFrames - 1);
        const int f0 = (int) fp;
        const int f1 = std::min (numFrames - 1, f0 + 1);
        const float ff = (float) (fp - (double) f0);
        const int n = size[(size_t) L];
        const double x = phase01 * (double) n;
        const int i = std::min (n - 1, std::max (0, (int) x));
        const float fr = (float) (x - (double) i);
        const float* a = level (f0, L);
        const float va = a[i] + fr * (a[i + 1] - a[i]);
        if (f1 == f0 || ff <= 0.0f) return va;
        const float* b = level (f1, L);
        const float vb = b[i] + fr * (b[i + 1] - b[i]);
        return va + ff * (vb - va);
    }
};

//==============================================================================
constexpr int kNumBuiltinTables = 6;
constexpr int kUserTableSlot = kNumBuiltinTables;     // Index der Nutzer-Wavetable
constexpr int kNumTableSlots = kNumBuiltinTables + 1;

/** Zeiger auf die für einen Block gültigen Inhalte (Besitz liegt außerhalb). */
struct Layers
{
    const KickSample* kickSample = nullptr;
    const KickSample* bassSample = nullptr;   // gleiches Datenformat, wird pro Note gestimmt
    std::array<const WavetableData*, kNumTableSlots> tables {};
};

} // namespace samsi
