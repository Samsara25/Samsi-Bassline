#include "SampleAlign.h"

#include "../dsp/DspUtils.h"
#include "../dsp/KickEngine.h"

#include <cmath>
#include <vector>

namespace samsi
{

namespace
{
    std::vector<double> lowpass (const std::vector<double>& x, double sr, double hz)
    {
        SvfCoeffs c; c.set (hz, sr, 1.41421356);
        SvfState s1, s2;
        std::vector<double> y (x.size());
        for (size_t i = 0; i < x.size(); ++i) y[i] = svfTick (s2, c, svfTick (s1, c, x[i]).lp).lp;
        return y;
    }

    /** Sample so abspielen, wie es der Kick-Sample-Layer tut (Start, Stimmung, Verzögerung), ohne Hüllkurve. */
    std::vector<double> playSample (const KickSample& smp, double sr, double startMs, double delayMs, double tuneSt, int n)
    {
        std::vector<double> y ((size_t) n, 0.0);
        const double step = smp.rate / sr * std::pow (2.0, tuneSt / 12.0);
        double pos = startMs * 0.001 * smp.rate;
        const int d0 = (int) std::lround (delayMs * 0.001 * sr);
        for (int i = d0; i < n; ++i)
        {
            const int k = (int) pos;
            if (k < 0 || k + 1 >= (int) smp.data.size()) break;
            const double fr = pos - k;
            y[(size_t) i] = smp.data[(size_t) k] * (1.0 - fr) + smp.data[(size_t) k + 1] * fr;
            pos += step;
        }
        return y;
    }

    double corr (const std::vector<double>& a, const std::vector<double>& b)
    {
        double ab = 0, aa = 0, bb = 0;
        for (size_t i = 0; i < a.size(); ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; }
        return aa > 0 && bb > 0 ? ab / std::sqrt (aa * bb) : 0.0;
    }
}

KickAlignResult alignKickSample (const KickSettings& kIn, const KickSample& smp, double sr)
{
    KickAlignResult r;
    if (smp.data.size() < 32 || smp.rate <= 0.0) return r;

    const int n = (int) (0.12 * sr);
    // Synth-Kick allein (Sample aus), mit eingeschwungenem Drive
    KickSettings k = kIn;
    k.smpOn = false;
    KickEngine e;
    e.prepare (sr);
    e.trigger (k, 1.0f, 1.0f, nullptr);
    std::vector<float> buf ((size_t) n);
    e.render (buf.data(), n, k);
    std::vector<double> synth (buf.begin(), buf.end());
    const auto synthLp = lowpass (synth, sr, 200.0);

    // Moduliert die Kick das Sample (Hüllkurve, Pitch, Key-Track, Sättigung), wird die Sample-Schicht mit der
    // echten Engine gerendert (gesamt minus Synth allein) – so wird genau das ausgerichtet, was man hört.
    const bool modulated = kIn.smpAmpEnv > 0.0f || kIn.smpPitchEnv > 0.0f || (kIn.smpKeyTrack && smp.pitchHz > 0.0f) || kIn.smpDrive;
    std::vector<float> full ((size_t) n);
    auto renderLayer = [&] (double startMs, double delayMs)
    {
        KickSettings ks = kIn;
        ks.smpOn = true; ks.smpInvert = false; ks.smpLevelDb = 0.0f;
        ks.smpStartMs = (float) startMs; ks.smpOffsetMs = (float) delayMs;
        KickEngine eng;
        eng.prepare (sr);
        eng.trigger (ks, 1.0f, 1.0f, &smp);
        eng.render (full.data(), n, ks);
        std::vector<double> d ((size_t) n);
        for (int i = 0; i < n; ++i) d[(size_t) i] = (double) full[(size_t) i] - synth[(size_t) i];
        return d;
    };
    auto evalAt = [&] (double startMs, double delayMs)
    {
        if (modulated) return corr (synthLp, lowpass (renderLayer (startMs, delayMs), sr, 200.0));
        return corr (synthLp, lowpass (playSample (smp, sr, startMs, delayMs, kIn.smpTuneSt, n), sr, 200.0));
    };

    const double c0 = evalAt (kIn.smpStartMs, kIn.smpOffsetMs);
    r.coherenceBefore = kIn.smpInvert ? -c0 : c0;

    // Gesamtversatz v (ms): v > 0 -> Sample später (Verzögerung), v < 0 -> früher (Startpunkt nach hinten)
    double speed = std::pow (2.0, (double) kIn.smpTuneSt / 12.0);
    if (kIn.smpKeyTrack && smp.pitchHz > 0.0f) speed *= (double) kIn.endHz / (double) smp.pitchHz;
    double bestC = -2.0, bestAbs = 0.0, bestV = 0.0;
    for (double v = -10.0; v <= 10.0 + 1e-9; v += 0.1)
    {
        const double start = v < 0.0 ? -v * speed : 0.0;
        const double delay = v > 0.0 ? v : 0.0;
        const double c = evalAt (start, delay);
        if (std::abs (c) > bestAbs + 1.0e-9) { bestAbs = std::abs (c); bestC = c; bestV = v; }
    }
    r.valid = true;
    r.invert = bestC < 0.0;
    r.startMs = (float) (bestV < 0.0 ? -bestV * speed : 0.0);
    r.delayMs = (float) (bestV > 0.0 ? bestV : 0.0);
    r.coherenceAfter = bestAbs;
    return r;
}

} // namespace samsi
