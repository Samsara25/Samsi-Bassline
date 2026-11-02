#include "Settings.h"

#include <algorithm>
#include <cstring>

namespace samsi
{

static int clampi (int v, int lo, int hi) noexcept { return std::min (hi, std::max (lo, v)); }

// Bitbelegung: 0 kick | 1 bass | 2 slide | 3-9 kickVel | 10-16 bassVel | 17-22 note+24 | 23-31 lenPct/5 (2..80)
uint32_t Step::pack() const noexcept
{
    uint32_t v = 0;
    v |= kick  ? 1u : 0u;
    v |= bass  ? 2u : 0u;
    v |= slide ? 4u : 0u;
    v |= (uint32_t) clampi (kickVel, 1, 127) << 3;
    v |= (uint32_t) clampi (bassVel, 1, 127) << 10;
    v |= (uint32_t) (clampi (note, -24, 24) + 24) << 17;
    v |= (uint32_t) clampi ((lenPct + 2) / 5, 2, 80) << 23;
    return v;
}

Step Step::unpack (uint32_t v) noexcept
{
    Step s;
    s.kick    = (v & 1u) != 0;
    s.bass    = (v & 2u) != 0;
    s.slide   = (v & 4u) != 0;
    s.kickVel = clampi ((int) ((v >> 3) & 127u), 1, 127);
    s.bassVel = clampi ((int) ((v >> 10) & 127u), 1, 127);
    s.note    = clampi ((int) ((v >> 17) & 63u) - 24, -24, 24);
    s.lenPct  = clampi ((int) ((v >> 23) & 127u) * 5, 10, 400);
    return s;
}

PatternData PatternData::makeEmpty() noexcept
{
    PatternData p;
    for (int i = 0; i < kNumSteps; ++i)
        p.set (i, Step {});
    return p;
}

PatternData PatternData::makeDefault() noexcept
{
    PatternData p;
    for (int i = 0; i < kNumSteps; ++i)
    {
        Step s;
        s.kick = (i % 4) == 0;
        s.bass = (i % 4) != 0;
        s.bassVel = (i % 4) == 1 ? 108 : 100;
        s.lenPct = 75;
        p.set (i, s);
    }
    return p;
}

PatternData PatternData::makeRolling (int grid, bool offbeat) noexcept
{
    const int per = stepsForGrid (grid) / 4;
    PatternData p = makeEmpty();
    for (int i = 0; i < stepsForGrid (grid); ++i)
    {
        const int pos = i % per;
        bool bass = false;
        if (per == 4)      bass = offbeat ? pos >= 2 : pos >= 1;
        else if (per == 6) bass = offbeat ? (pos == 2 || pos >= 4) : pos >= 2;
        else               bass = offbeat ? pos >= 4 : pos >= 3;
        Step s;
        s.kick = pos == 0;
        s.bass = bass;
        s.bassVel = (bass && (i == 0 || ! p.get (i - 1).bass)) ? 110 : 98;
        s.lenPct = offbeat ? 65 : 75;
        p.set (i, s);
    }
    return p;
}

PatternData PatternData::makeOffbeat() noexcept
{
    PatternData p;
    for (int i = 0; i < kNumSteps; ++i)
    {
        Step s;
        s.kick = (i % 4) == 0;
        s.bass = (i % 4) == 2 || (i % 4) == 3;
        s.lenPct = 60;
        p.set (i, s);
    }
    return p;
}

//==============================================================================
namespace
{
    struct Fnv
    {
        uint64_t h = 1469598103934665603ull;
        void bytes (const void* d, size_t n) noexcept
        {
            auto* b = static_cast<const unsigned char*> (d);
            for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
        }
        void f (float v) noexcept   { bytes (&v, sizeof v); }
        void i (int v) noexcept     { bytes (&v, sizeof v); }
        void d (double v) noexcept  { bytes (&v, sizeof v); }
    };
}

uint64_t soundHash (const Settings& s, const PatternData& p, double bpm, double sampleRate, uint64_t layerId) noexcept
{
    Fnv h;
    const auto& k = s.kick;
    for (float v : { k.levelDb, k.startHz, k.endHz, k.pitchMs, k.pitchCurve, k.attackMs, k.holdMs, k.decayMs,
                     k.decayCurve, k.tailLevel, k.tailMs, k.startPhaseDeg, k.clickLevelDb, k.clickDecayMs,
                     k.clickToneHz, k.driveDb, k.smpLevelDb, k.smpTuneSt, k.smpOffsetMs, k.smpStartMs, k.smpLengthMs,
                     k.smpFadeMs, k.smpLowCutHz, k.smpAmpEnv, k.smpPitchEnv })
        h.f (v);
    h.i (k.smpOn ? 1 : 0);
    h.i (k.smpInvert ? 1 : 0);
    h.i (k.smpKeyTrack ? 1 : 0);
    h.i (k.smpDrive ? 1 : 0);

    const auto& b = s.bass;
    for (float v : { b.levelDb, b.saw, b.square, b.pulseWidth, b.sub, b.startPhaseDeg, b.cutoffHz, b.resonance,
                     b.envAmountOct, b.keytrack, b.fAttackMs, b.fDecayMs, b.fSustain, b.fReleaseMs, b.attackMs,
                     b.decayMs, b.sustain, b.releaseMs, b.velToAmp, b.velToFilter, b.driveDb, b.glideMs,
                     b.width, b.widthHz, b.grooveDelayMs, b.wtLevel, b.wtPos, b.wtEnvAmt, b.pickAmount, b.pickPos, b.damper,
                     b.smpLevel, b.smpTuneSt, b.smpStartMs, b.smpPitchEnvSt })
        h.f (v);
    h.i (b.wtTable);
    h.i (b.subOctave);
    h.i (b.phaseReset ? 1 : 0);
    h.i ((int) b.filterMode);
    h.i (b.smpThru ? 1 : 0);

    h.f (s.global.swing);
    h.i (s.global.rootNote);
    h.i (s.global.seqOn ? 1 : 0);
    h.i (s.global.seqGrid);
    // Fit-Ducking-Frequenz/Modus sind Nutzervorgaben, kein Optimierungsergebnis
    h.i (s.fit.duckLowOnly ? 1 : 0);
    h.f (s.fit.duckHz);

    const auto& g = s.glue;
    h.i (g.on ? 1 : 0);
    if (g.on)
    {
        h.i (g.compOn ? 1 : 0); h.i (g.style);
        for (float v : { g.threshDb, g.ratio, g.attackMs, g.releaseMs, g.kneeDb, g.rangeDb, g.scHpHz, g.scLpHz, g.mix }) h.f (v);
        for (const auto& b : g.band)
        {
            h.i (b.on ? 1 : 0); h.i (b.type);
            for (float v : { b.freqHz, b.q, b.rangeDb, b.threshDb, b.attackMs, b.releaseMs }) h.f (v);
        }
    }

    h.bytes (p.steps.data(), sizeof (uint32_t) * p.steps.size());
    h.bytes (&layerId, sizeof layerId);
    h.d (bpm);
    h.d (sampleRate);
    return h.h;
}

} // namespace samsi
