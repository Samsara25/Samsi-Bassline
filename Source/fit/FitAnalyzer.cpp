#include "FitAnalyzer.h"

#include "../dsp/DspUtils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace samsi
{

namespace
{
    /** Bandpass 4. Ordnung aus zwei Butterworth-SVF-Stufen (Hochpass dann Tiefpass). */
    std::vector<float> bandFilter (const std::vector<float>& x, double sr, double loHz, double hiHz)
    {
        SvfCoeffs hp, lp;
        hp.set (loHz, sr, 1.41421356);
        lp.set (hiHz, sr, 1.41421356);
        SvfState h1, h2, l1, l2;
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
        {
            double v = svfTick (h1, hp, x[i]).hp;
            v = svfTick (h2, hp, v).hp;
            v = svfTick (l1, lp, v).lp;
            v = svfTick (l2, lp, v).lp;
            y[i] = (float) v;
        }
        return y;
    }

    double energy (const std::vector<float>& x, int a, int b) noexcept
    {
        a = std::max (0, a);
        b = std::min ((int) x.size(), b);
        double e = 0.0;
        for (int i = a; i < b; ++i) e += (double) x[(size_t) i] * (double) x[(size_t) i];
        return e;
    }

    double peakAbs (const std::vector<float>& x) noexcept
    {
        double p = 0.0;
        for (float v : x) p = std::max (p, (double) std::abs (v));
        return p;
    }

    /** Biquad für die K-Gewichtung nach ITU-R BS.1770. */
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double tick (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void kWeighting (double fs, Biquad& pre, Biquad& rlb) noexcept
    {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / fs);
            const double Vh = std::pow (10.0, G / 20.0);
            const double Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            pre.b0 = (Vh + Vb * K / Q + K * K) / a0;
            pre.b1 = 2.0 * (K * K - Vh) / a0;
            pre.b2 = (Vh - Vb * K / Q + K * K) / a0;
            pre.a1 = 2.0 * (K * K - 1.0) / a0;
            pre.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (kPi * f0 / fs);
            const double a0 = 1.0 + K / Q + K * K;
            rlb.b0 = 1.0; rlb.b1 = -2.0; rlb.b2 = 1.0;
            rlb.a1 = 2.0 * (K * K - 1.0) / a0;
            rlb.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }

    double loudness (const std::vector<float>& l, const std::vector<float>& r, double fs)
    {
        double sum = 0.0;
        for (const auto* ch : { &l, &r })
        {
            Biquad pre, rlb;
            kWeighting (fs, pre, rlb);
            double ms = 0.0;
            for (float v : *ch)
            {
                const double y = rlb.tick (pre.tick (v));
                ms += y * y;
            }
            sum += ms / (double) std::max<size_t> (1, ch->size());
        }
        return -0.691 + powerToDb (sum);
    }

    double relu (double x) noexcept { return x > 0.0 ? x : 0.0; }
}

//==============================================================================
FitMetrics analyze (const RenderResult& r, const FitReference& ref, const FitWeights& w, bool detail)
{
    FitMetrics m;
    const int len = r.length();
    if (len <= 0) return m;

    const double sr = r.sampleRate;
    const double bandLo[kNumBands] = { 20.0, 60.0, 120.0 };
    const double bandHi[kNumBands] = { 60.0, 120.0, 250.0 };

    std::array<std::vector<float>, kNumBands> kb, bb, sb;
    for (int j = 0; j < kNumBands; ++j)
    {
        kb[(size_t) j] = bandFilter (r.kick, sr, bandLo[j], bandHi[j]);
        bb[(size_t) j] = bandFilter (r.bass, sr, bandLo[j], bandHi[j]);
        sb[(size_t) j] = bandFilter (r.mix,  sr, bandLo[j], bandHi[j]);
    }
    // 20-120 Hz kombiniert (Summe der zwei unteren Bänder)
    auto addBands = [] (const std::vector<float>& a, const std::vector<float>& b)
    {
        std::vector<float> y (a.size());
        for (size_t i = 0; i < a.size(); ++i) y[i] = a[i] + b[i];
        return y;
    };
    const auto kLow = addBands (kb[0], kb[1]);
    const auto bLow = addBands (bb[0], bb[1]);
    const auto sLow = addBands (sb[0], sb[1]);

    //--- Peaks / Headroom ----------------------------------------------------
    m.kickPeakDb = gainToDb (peakAbs (r.kick));
    m.bassPeakDb = gainToDb (peakAbs (r.bass));
    m.sumPeakDb  = gainToDb (std::max (peakAbs (r.mixL), peakAbs (r.mixR)));
    m.headroomDb = -m.sumPeakDb;
    m.loudnessLu = loudness (r.mixL, r.mixR, sr);

    //--- Überlappungsfenster (Kick-Ereignisse) -------------------------------
    std::vector<int> kicks = r.kickOn;
    std::sort (kicks.begin(), kicks.end());
    const int frame = std::max (8, (int) std::lround (0.010 * sr));

    for (size_t i = 0; i < kicks.size(); ++i)
    {
        const int a = kicks[i];
        int b = a + (int) r.kickLengthSamples;
        if (i + 1 < kicks.size()) b = std::min (b, kicks[i + 1]);
        b = std::min (b, len);
        const int a0 = std::max (0, a);
        if (b > a0) m.overlapWindows.push_back ({ a0, b });
    }

    double attackLossSum = 0.0; int attackCount = 0;
    for (int j = 0; j < kNumBands; ++j)
    {
        // Schwelle: -40 dB unter dem lautesten Frame (Kick+Bass) im Band
        double maxFrame = 0.0;
        for (int p = 0; p + frame <= len; p += frame)
            maxFrame = std::max (maxFrame, energy (kb[(size_t) j], p, p + frame) + energy (bb[(size_t) j], p, p + frame));
        const double thr = maxFrame * 1.0e-4;

        double wsum = 0.0, cancelW = 0.0, cohW = 0.0, worst = 0.0;
        for (const auto& win : m.overlapWindows)
        {
            for (int p = win.first; p < win.second; p += frame)
            {
                const int q = std::min (win.second, p + frame);
                const double ek = energy (kb[(size_t) j], p, q);
                const double eb = energy (bb[(size_t) j], p, q);
                if (ek < thr || eb < thr) continue;
                const double es = energy (sb[(size_t) j], p, q);
                const double c = powerToDb (es) - powerToDb (ek + eb);
                const double weight = ek + eb;
                wsum += weight;
                cancelW += weight * relu (-c);
                cohW += weight * c;
                worst = std::max (worst, std::min (24.0, relu (-c)));
            }
        }
        m.cancelMeanDb[(size_t) j]  = wsum > 0.0 ? cancelW / wsum : 0.0;
        m.coherenceDb[(size_t) j]   = wsum > 0.0 ? cohW / wsum : 0.0;
        m.cancelWorstDb[(size_t) j] = worst;

        m.kickBandDb[(size_t) j] = powerToDb (energy (kb[(size_t) j], 0, len) / len);
        m.bassBandDb[(size_t) j] = powerToDb (energy (bb[(size_t) j], 0, len) / len);
        m.sumBandDb[(size_t) j]  = powerToDb (energy (sb[(size_t) j], 0, len) / len);
    }

    //--- Kick-Attack (erste 8 ms, Vollband) -----------------------------------
    const int atk = std::max (1, (int) std::lround (0.008 * sr));
    for (int k : kicks)
    {
        if (k < 0 || k >= len) continue;
        const double ek = energy (r.kick, k, k + atk);
        const double es = energy (r.mix, k, k + atk);
        if (ek <= 0.0) continue;
        attackLossSum += relu (powerToDb (ek) - powerToDb (es));
        ++attackCount;
    }
    m.kickAttackLossDb = attackCount > 0 ? attackLossSum / attackCount : 0.0;

    //--- Bassnoten-Beginn und Gleichmäßigkeit --------------------------------
    std::vector<int> ons;
    for (int b : r.bassOn) if (b >= 0 && b < len) ons.push_back (b);
    std::sort (ons.begin(), ons.end());
    const int onsetLen = std::max (1, (int) std::lround (0.015 * sr));
    const int noteLen  = std::max (1, (int) std::lround (0.060 * sr));
    double cancelSum = 0.0, maskSum = 0.0;
    std::vector<double> noteDb;
    const double bLowMax = [&] { double mx = 0; for (float v : bLow) mx = std::max (mx, (double) v * v); return mx; }();
    for (size_t i = 0; i < ons.size(); ++i)
    {
        const int a = ons[i];
        const double eb = energy (bLow, a, a + onsetLen);
        const double ek = energy (kLow, a, a + onsetLen);
        const double es = energy (sLow, a, a + onsetLen);
        if (eb > bLowMax * 1.0e-6 * onsetLen)
        {
            cancelSum += relu (powerToDb (ek + eb) - powerToDb (es));
            maskSum   += relu (powerToDb (ek) - powerToDb (eb));
        }
        int end = a + noteLen;
        if (i + 1 < ons.size()) end = std::min (end, ons[i + 1]);
        double en = 0.0;
        for (int j = 0; j < kNumBands; ++j) en += energy (bb[(size_t) j], a, end);
        noteDb.push_back (powerToDb (en / std::max (1, end - a)));
    }
    if (! ons.empty())
    {
        m.onsetCancelDb = cancelSum / (double) ons.size();
        m.onsetMaskDb   = maskSum / (double) ons.size();
        const double mean = std::accumulate (noteDb.begin(), noteDb.end(), 0.0) / (double) noteDb.size();
        double var = 0.0;
        for (double d : noteDb) var += (d - mean) * (d - mean);
        m.noteSigmaDb = std::sqrt (var / (double) noteDb.size());
    }

    //--- Energie / Tiefband-Überhöhung ---------------------------------------
    {
        double eb = 0.0, ek = 0.0;
        for (int j = 0; j < kNumBands; ++j) { eb += energy (bb[(size_t) j], 0, len); ek += energy (kb[(size_t) j], 0, len); }
        m.bassEnergyDb = powerToDb (eb / len);
        m.kickEnergyDb = powerToDb (ek / len);

        const int fr = std::max (8, (int) std::lround (0.005 * sr));
        double maxSum = 0.0, maxRef = 0.0;
        for (int p = 0; p + fr <= len; p += fr)
        {
            maxSum = std::max (maxSum, energy (sLow, p, p + fr));
            maxRef = std::max (maxRef, energy (kLow, p, p + fr) + energy (bLow, p, p + fr));
        }
        m.lowBumpDb = powerToDb (maxSum) - powerToDb (maxRef);

        if (detail)
        {
            m.frameHopSamples = fr;
            double mx = 0.0;
            for (int p = 0; p + fr <= len; p += fr)
                mx = std::max (mx, energy (kLow, p, p + fr) + energy (bLow, p, p + fr));
            for (int p = 0; p + fr <= len; p += fr)
            {
                const double fk = energy (kLow, p, p + fr), fb = energy (bLow, p, p + fr);
                if (fk < mx * 1.0e-4 || fb < mx * 1.0e-4)
                    m.lowCoherenceFrames.push_back (std::numeric_limits<float>::quiet_NaN());
                else
                    m.lowCoherenceFrames.push_back ((float) (powerToDb (energy (sLow, p, p + fr)) - powerToDb (fk + fb)));
            }
        }
    }

    //--- Zielfunktion --------------------------------------------------------
    auto& T = m.terms;
    for (int j = 0; j < kNumBands; ++j)
    {
        const double mean = m.cancelMeanDb[(size_t) j], worst = m.cancelWorstDb[(size_t) j];
        T[0] += w.band[(size_t) j] * (mean * mean + w.worstFactor * worst * worst);
    }
    const double refPeak = std::max (m.kickPeakDb, m.bassPeakDb) + w.peakAllowanceDb;
    T[1] = w.peak * std::pow (relu (m.sumPeakDb - refPeak), 2.0);
    T[2] = w.lowBump * std::pow (relu (m.lowBumpDb - w.lowBumpAllowanceDb), 2.0);
    T[3] = w.attack * m.kickAttackLossDb * m.kickAttackLossDb;
    T[4] = w.onsetCancel * m.onsetCancelDb * m.onsetCancelDb;
    T[5] = w.onsetMask * std::pow (relu (m.onsetMaskDb - w.onsetMaskAllowDb), 2.0);

    if (ref.valid)
    {
        T[6] = w.evenness * std::pow (relu (m.noteSigmaDb - ref.noteSigmaDb), 2.0);
        const double loss = ref.bassEnergyDb - m.bassEnergyDb;
        T[7] = w.bassLevel * std::pow (relu (loss - w.bassLossAllowDb), 2.0);
        if (loss > w.bassLossRejectDb) T[7] += 1.0e6;   // Optimierer darf den Bass nicht "wegducken"
    }
    double J = 0.0;
    for (double t : T) J += t;
    m.cost = J;
    return m;
}

double changeCost (const FitSettings& f, float /*userPhaseDeg*/) noexcept
{
    // Die Startphase selbst ist "frei" (verändert nur den Notenbeginn, nicht den Grundklang).
    double c = 0.0;
    c += 0.15  * (double) f.offsetMs * (double) f.offsetMs;
    c += 0.02  * (double) f.duckDb * (double) f.duckDb;
    c += 0.6   * (double) f.trimDb * (double) f.trimDb;
    c += 0.05  * (double) f.attackAddMs * (double) f.attackAddMs;
    c += 0.004 * std::pow (100.0 - (double) f.kickTailPct, 2.0);
    c += f.polarity ? 0.2 : 0.0;
    c += f.phaseMode == PhaseMode::FollowKick ? 0.2 : 0.0;
    return c;
}

} // namespace samsi
