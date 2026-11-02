#pragma once

#include "DspUtils.h"
#include "Settings.h"

#include <array>
#include <vector>

namespace samsi
{

//==============================================================================
/** 90-Grad-Phasenpaar aus zwei Allpass-Ketten (Struktur nach O. Niemitalo, Koeffizienten selbst
    berechnet, siehe tools/hilbert_design.py). Zweig A eilt Zweig B um 90° voraus, 15 Hz–20 kHz,
    Fehler < 0,05°. Phasendrehung um θ: y = cos θ · A − sin θ · B.
    Hinweis: Auch bei θ = 0 ist A selbst ein Allpass (frequenzabhängige Laufzeit) – darum ist
    die Phasendrehung nur aktiv, wenn sie eingeschaltet ist. */
class HilbertPair
{
public:
    static constexpr int kSections = 8;
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    /** liefert a (0°-Zweig) und b (-90°-Zweig) */
    inline void process (float x, float& a, float& b) noexcept
    {
        double va = x, vb = xd;   // Zweig B mit einer Sample-Verzögerung
        xd = x;
        for (int i = 0; i < kSections; ++i)
        {
            auto& s = sa[(size_t) i];
            const double y = ca[(size_t) i] * (va + s.y2) - s.x2;
            s.x2 = s.x1; s.x1 = va; s.y2 = s.y1; s.y1 = y;
            va = y;
            auto& t = sb[(size_t) i];
            const double z = cb[(size_t) i] * (vb + t.y2) - t.x2;
            t.x2 = t.x1; t.x1 = vb; t.y2 = t.y1; t.y1 = z;
            vb = z;
        }
        a = (float) va; b = (float) vb;
    }

private:
    struct St { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };
    std::array<double, kSections> ca {}, cb {};
    std::array<St, kSections> sa {}, sb {};
    double xd = 0.0;
};

//==============================================================================
/** Verzögerungsleitung mit gebrochener Verzögerung (kubische Hermite-Interpolation,
    bei ganzzahliger Verzögerung exakt). Speicher wird nur in prepare() angelegt. */
class FracDelay
{
public:
    void prepare (int maxSamples);
    void reset() noexcept;
    inline void push (float x) noexcept
    {
        w = (w + 1) & mask;
        buf[(size_t) w] = x;
    }
    /** Wert von vor d Samples (0 = zuletzt geschrieben), d >= 0 */
    inline float read (double d) const noexcept
    {
        d = std::min (std::max (0.0, d), (double) maxDelay);
        const int di = (int) d;
        const float fr = (float) (d - (double) di);
        auto at = [this] (int k) { return buf[(size_t) ((w - k) & mask)]; };
        if (fr == 0.0f) return at (di);
        const float y0 = at (di - 1 < 0 ? 0 : di - 1), y1 = at (di), y2 = at (di + 1), y3 = at (di + 2);
        // Hermite zwischen y1 (d = di) und y2 (d = di + 1)
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                    c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * fr + c2) * fr + c1) * fr + c0;
    }
    int capacity() const noexcept { return maxDelay; }

private:
    std::vector<float> buf;
    int mask = 0, w = 0, maxDelay = 0;
};

//==============================================================================
/** Lineare Ausgangs-Utility für EINEN Stereo-Bus. Dieselbe Instanz-Konfiguration läuft auf
    MIX, KICK und BASS – da jede Stufe linear und zeitinvariant ist (bei festen Reglern),
    bleibt MIX = KICK + BASS exakt erhalten.
    Kette (nass): DC-Sperre → Mono-Filter (nur Seitensignal) → Imager → Verzögerung
    (Grundlatenz + Time Shift + L/R-Tilt) → Phasendrehung → Swap → Invert → Listen;
    dann Mix mit dem latenzgleich verzögerten Trockensignal und Ausgangspegel L/R. */
class BusUtility
{
public:
    static constexpr double kTimeShiftBaseMs = 50.0;
    static constexpr double kMaxTiltMs = 20.0;

    void prepare (double sampleRate, int maxBlock);
    void reset (const UtilitySettings& u) noexcept;
    /** baseDelaySamples: Grundlatenz der Utility (Time Shift ein -> 50 ms, sonst 0) */
    void process (float* l, float* r, int n, const UtilitySettings& u, int baseDelaySamples) noexcept;

    static int baseLatencySamples (const UtilitySettings& u, double sampleRate) noexcept
    {
        return u.on && u.timeShiftOn ? (int) std::lround (kTimeShiftBaseMs * 0.001 * sampleRate) : 0;
    }

private:
    void updateFilters (const UtilitySettings& u) noexcept;

    double sr = 48000.0;
    // DC-Sperre (1. Ordnung, 5 Hz)
    double dcR = 0.9993, dcX[2] {}, dcY[2] {};
    // Mono-Filter: Hochpass 4. Ordnung (2 x Butterworth 2. Ordnung) auf dem Seitensignal
    struct Biquad { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        inline double tick (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    Biquad monoHp[2];
    float lastMonoHz = -1.0f;
    HilbertPair hil[2];
    FracDelay dl[2], dry[2];
    LinearSmoother imager, cosT, sinT, mix, delL, delR;
    GainSmoother outL, outR;
    bool prepared = false;
    int lastBase = 0;
};

//==============================================================================
/** Brickwall-Limiter mit Vorausschau (nur MIX, nichtlinear). Garantiert |y| <= Ceiling:
    Min-Filter der benötigten Verstärkung über das Vorausschau-Fenster, Release-Glättung nach
    unten begrenzt, dann gleitender Mittelwert über das Fenster (jeder gemittelte Wert ist
    <= der am Ausgangszeitpunkt nötigen Verstärkung). Latenz = Fenster-Länge. */
class Limiter
{
public:
    static constexpr double kLookaheadMs = 1.5;
    void prepare (double sampleRate);
    void reset() noexcept;
    static int latencySamples (double sampleRate) noexcept { return std::max (1, (int) std::lround (kLookaheadMs * 0.001 * sampleRate)); }
    void process (float* l, float* r, int n, float ceilingDb) noexcept;
    float getGainReductionDb() const noexcept { return grDb; }

private:
    int la = 64;
    std::vector<float> dL, dR, gHist, boxHist;
    int pos = 0;
    double boxSum = 0.0;
    float rel = 0.0f, gr = 1.0f, grDb = 0.0f;
};

/** Reine Verzögerung (für KICK/BASS, damit sie bei aktivem Limiter zeitgleich zu MIX bleiben). */
class PlainDelay
{
public:
    void prepare (int samples) { buf.assign ((size_t) std::max (1, samples), 0.0f); pos = 0; }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
    inline float tick (float x) noexcept { const float y = buf[(size_t) pos]; buf[(size_t) pos] = x; pos = (pos + 1) % (int) buf.size(); return y; }
private:
    std::vector<float> buf;
    int pos = 0;
};

} // namespace samsi
