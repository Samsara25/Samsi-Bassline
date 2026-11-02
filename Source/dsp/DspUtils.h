#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wfloat-equal"   // exakte Vergleiche hier beabsichtigt (Zielwert-Caching)
#endif

namespace samsi
{

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline float dbToGain (float db) noexcept      { return db <= -100.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline double gainToDb (double g) noexcept     { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }
inline double powerToDb (double p) noexcept    { return 10.0 * std::log10 (std::max (p, 1.0e-20)); }
inline int msToSamples (double ms, double sr) noexcept { return (int) std::lround (std::max (0.0, ms) * 0.001 * sr); }

/** Ungültige Zahlen (NaN/Inf) durch einen sicheren Wert ersetzen und begrenzen. */
inline float sanitize (float v, float lo, float hi, float fallback) noexcept
{
    if (! std::isfinite (v)) return fallback;
    return std::min (hi, std::max (lo, v));
}

inline double wrap01 (double p) noexcept
{
    p -= std::floor (p);
    return p >= 1.0 ? 0.0 : p;
}

//==============================================================================
/** Linearer Parameter-Glätter (klickfrei, deterministisch). */
class LinearSmoother
{
public:
    void prepare (double sampleRate, double rampMs) noexcept
    {
        rampSamples = std::max (1, (int) std::lround (rampMs * 0.001 * sampleRate));
    }
    void reset (float v) noexcept { current = target = v; remaining = 0; step = 0.0f; }
    void setTarget (float v) noexcept
    {
        if (v == target) return;
        target = v;
        remaining = rampSamples;
        step = (target - current) / (float) remaining;
    }
    float next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0) current = target;
        }
        return current;
    }
    float getCurrent() const noexcept { return current; }
    bool isSmoothing() const noexcept { return remaining > 0; }

private:
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampSamples = 1;
};

//==============================================================================
/** Multiplikativer Gain-Glätter: gleichmäßige dB-Rampe (für Pegel/Master/Trim).
    Erwartet Zielwerte > 0 (alle Pegelparameter haben endliche dB-Untergrenzen). */
class GainSmoother
{
public:
    void prepare (double sampleRate, double rampMs) noexcept
    {
        rampSamples = std::max (1, (int) std::lround (rampMs * 0.001 * sampleRate));
    }
    void reset (float v) noexcept { current = target = std::max (v, 1.0e-6f); remaining = 0; factor = 1.0; }
    void setTarget (float v) noexcept
    {
        v = std::max (v, 1.0e-6f);
        if (v == target) return;
        target = v;
        remaining = rampSamples;
        factor = std::pow ((double) target / (double) current, 1.0 / (double) remaining);
    }
    bool isSmoothing() const noexcept { return remaining > 0; }
    float next() noexcept
    {
        if (remaining > 0)
        {
            current = (float) ((double) current * factor);
            if (--remaining == 0) current = target;
        }
        return current;
    }

private:
    float current = 1.0f, target = 1.0f;
    double factor = 1.0;
    int remaining = 0, rampSamples = 1;
};

//==============================================================================
/** Zustandsvariables Filter in TPT-Struktur (Zavalishin / Simper).
    Stabil bei schneller Modulation, liefert LP/BP/HP gleichzeitig. */
struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;
    void reset() noexcept { ic1 = ic2 = 0.0; }
};

struct SvfCoeffs
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.4142;

    static double prewarp (double cutoffHz, double sampleRate) noexcept
    {
        const double fc = std::min (std::max (cutoffHz, 5.0), sampleRate * 0.49);
        return std::tan (kPi * fc / sampleRate);
    }

    void set (double cutoffHz, double sampleRate, double kDamping) noexcept
    {
        setG (prewarp (cutoffHz, sampleRate), kDamping);
    }

    void setG (double g, double kDamping) noexcept
    {
        k  = kDamping;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct SvfOut { double lp, bp, hp; };

inline SvfOut svfTick (SvfState& s, const SvfCoeffs& c, double v0) noexcept
{
    const double v3 = v0 - s.ic2;
    const double v1 = c.a1 * s.ic1 + c.a2 * v3;
    const double v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
    s.ic1 = 2.0 * v1 - s.ic1;
    s.ic2 = 2.0 * v2 - s.ic2;
    return { v2, v1, v0 - c.k * v1 - v2 };
}

//==============================================================================
/** tanh-Sättigung mit Antiderivative-Antialiasing erster Ordnung (ADAA).
    Reduziert Aliasing deutlich gegenüber naivem tanh, ohne Oversampling-Latenz
    (Parker et al., DAFx 2016). Erzeugt eine konstante Verzögerung von 0,5 Samples. */
class AdaaTanh
{
public:
    void reset() noexcept { xPrev = 0.0; cacheValid = false; }

    /** drive: Faktor >0. Ausgang ist auf tanh(drive) normiert (Vollaussteuerung bleibt ~0 dBFS). */
    double process (double x, double drive) noexcept
    {
        if (drive != cachedDrive)
        {
            cachedDrive = drive;
            invNorm = 1.0 / std::tanh (drive);
            cacheValid = false;
        }
        const double u = x * drive, uPrev = xPrev * drive;
        const double Fu = logCosh (u);
        const double FuPrev = cacheValid ? cachedF : logCosh (uPrev);
        double y;
        const double du = u - uPrev;
        if (std::abs (du) < 1.0e-6)
            y = std::tanh (0.5 * (u + uPrev));
        else
            y = (Fu - FuPrev) / du;
        xPrev = x;
        cachedF = Fu;
        cacheValid = true;
        return y * invNorm;
    }

    static double logCosh (double x) noexcept
    {
        const double ax = std::abs (x);
        if (ax > 18.0) return ax - 0.69314718055994530942;
        return std::log (std::cosh (x));
    }

private:
    double xPrev = 0.0, cachedDrive = -1.0, invNorm = 1.0, cachedF = 0.0;
    bool cacheValid = false;
};

/** Drive in dB -> tanh-Faktor; 0 dB ergibt nahezu lineares Verhalten (stetig). */
inline double driveDbToFactor (double db) noexcept
{
    return 0.05 + (std::pow (10.0, db / 20.0) - 1.0);
}

//==============================================================================
/** PolyBLEP-Korrektur für bandbegrenzte Saw/Square (Välimäki & Huovilainen). */
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)       { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

/** Deterministischer Rauschgenerator (reproduzierbar pro Trigger). */
struct Lcg
{
    uint32_t state = 0x12345678u;
    void seed (uint32_t s) noexcept { state = s ? s : 0x12345678u; }
    float next() noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) ((double) (state >> 8) / 8388608.0 - 1.0);
    }
};

} // namespace samsi

#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic pop
#endif
