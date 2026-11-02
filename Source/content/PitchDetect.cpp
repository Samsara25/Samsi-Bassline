#include "PitchDetect.h"

#include <algorithm>
#include <cmath>

namespace samsi
{

PitchResult detectPitch (const std::vector<float>& x, double rate, int start, int length, double minHz, double maxHz)
{
    PitchResult res;
    if (rate <= 0.0 || x.empty() || minHz <= 0.0 || maxHz <= minHz) return res;
    start = std::max (0, start);
    length = std::min (length, (int) x.size() - start);
    if (length < 64) return res;

    // Tiefpass (3 x einpolig bei 2 x maxHz) und Heruntertakten
    const int D = std::max (1, (int) std::floor (rate / (maxHz * 8.0)));
    const double fsD = rate / D;
    const double a = 1.0 - std::exp (-2.0 * 3.14159265358979 * std::min (maxHz * 2.0, rate * 0.45) / rate);
    double s1 = 0, s2 = 0, s3 = 0;
    std::vector<double> y;
    y.reserve ((size_t) (length / D + 1));
    for (int i = 0; i < length; ++i)
    {
        const double v = x[(size_t) (start + i)];
        s1 += a * (v - s1); s2 += a * (s1 - s2); s3 += a * (s2 - s3);
        if (i % D == 0) y.push_back (s3);
    }
    double mean = 0; for (double v : y) mean += v; mean /= (double) y.size();
    for (double& v : y) v -= mean;

    const int tauMin = std::max (2, (int) std::floor (fsD / maxHz));
    const int tauMax = (int) std::ceil (fsD / minHz);
    const int n = (int) y.size();
    if (n < tauMax * 2 + 4) return res;
    const int W = n - tauMax;

    std::vector<double> nsdf ((size_t) tauMax + 2, 0.0);
    for (int tau = tauMin - 1; tau <= tauMax + 1 && tau < n - W; ++tau)
    {
        double acf = 0, m = 0;
        for (int i = 0; i < W; ++i)
        {
            acf += y[(size_t) i] * y[(size_t) (i + tau)];
            m += y[(size_t) i] * y[(size_t) i] + y[(size_t) (i + tau)] * y[(size_t) (i + tau)];
        }
        nsdf[(size_t) std::max (0, tau)] = m > 1.0e-20 ? 2.0 * acf / m : 0.0;
    }

    // lokale Maxima
    double globalMax = 0.0;
    for (int tau = tauMin; tau <= tauMax; ++tau) globalMax = std::max (globalMax, nsdf[(size_t) tau]);
    if (globalMax < 0.3) return res;

    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
    {
        const double v = nsdf[(size_t) tau];
        if (v >= 0.9 * globalMax && v >= nsdf[(size_t) tau - 1] && v >= nsdf[(size_t) tau + 1])
        {
            best = tau;
            break;
        }
    }
    if (best < 0) return res;

    const double ym = nsdf[(size_t) best - 1], y0 = nsdf[(size_t) best], yp = nsdf[(size_t) best + 1];
    const double denom = ym - 2.0 * y0 + yp;
    const double shift = std::abs (denom) > 1.0e-12 ? 0.5 * (ym - yp) / denom : 0.0;
    const double period = (double) best + std::max (-0.5, std::min (0.5, shift));
    res.hz = fsD / period;
    res.confidence = std::min (1.0, std::max (0.0, y0));
    return res;
}

PitchResult detectKickPitch (const std::vector<float>& x, double rate)
{
    if (x.empty() || rate <= 0.0) return {};
    int peakIdx = 0;
    float peak = 0.0f;
    const int searchLen = std::min ((int) x.size(), (int) (0.15 * rate));
    for (int i = 0; i < searchLen; ++i)
        if (std::abs (x[(size_t) i]) > peak) { peak = std::abs (x[(size_t) i]); peakIdx = i; }

    // Ende: wo das Signal unter -40 dB des Maximums fällt
    int end = (int) x.size();
    const float thr = peak * 0.01f;
    for (int i = (int) x.size() - 1; i > peakIdx; --i)
        if (std::abs (x[(size_t) i]) > thr) { end = i; break; }

    const int start = peakIdx + (int) (0.07 * rate);
    const int len = std::min (end - start, (int) (0.4 * rate));
    auto r = detectPitch (x, rate, start, len, 25.0, 200.0);
    if (r.hz <= 0.0 && start > peakIdx)   // kurzer Kick: ab Maximum versuchen
        r = detectPitch (x, rate, peakIdx, std::min (end - peakIdx, (int) (0.4 * rate)), 25.0, 200.0);
    return r;
}

} // namespace samsi
