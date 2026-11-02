#include "WavetableFactory.h"

#include "PitchDetect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace samsi
{

namespace
{
    constexpr double kPiD = 3.14159265358979323846;
    constexpr int kCycleN = 8192;          // Analyseauflösung eines Zyklus
    constexpr int kBuiltinFrames = 32;

    uint64_t hashFloats (const std::vector<float>& v)
    {
        uint64_t h = 1469598103934665603ull;
        for (float f : v)
        {
            uint32_t b; static_assert (sizeof b == sizeof f, "");
            std::memcpy (&b, &f, sizeof b);
            h = (h ^ b) * 1099511628211ull;
        }
        return h;
    }

    /** Obertonkoeffizienten c[h] (h = 1..400) eines Zyklus: x(t) = sum 2*Re(c[h] e^{i2πht}) */
    std::vector<std::complex<double>> harmonicsOf (const std::vector<double>& cycle)
    {
        const int n = (int) cycle.size();
        std::vector<std::complex<double>> H ((size_t) WavetableData::kMaxHarm[0] + 1);
        const bool pow2 = n > 0 && (n & (n - 1)) == 0;
        if (pow2 && n >= 2 * (WavetableData::kMaxHarm[0] + 1))
        {
            std::vector<std::complex<double>> a (cycle.begin(), cycle.end());
            fft (a, false);
            for (size_t h = 1; h < H.size(); ++h) H[h] = a[h] / (double) n;
        }
        else
        {
            // direkte DFT für beliebige Längen (nur die benötigten Obertöne)
            const int hmax = std::min ((int) H.size() - 1, n / 2 - 1);
            for (int h = 1; h <= hmax; ++h)
            {
                std::complex<double> acc { 0.0, 0.0 };
                for (int i = 0; i < n; ++i)
                {
                    const double ph = -2.0 * kPiD * (double) h * (double) i / (double) n;
                    acc += cycle[(size_t) i] * std::complex<double> (std::cos (ph), std::sin (ph));
                }
                H[(size_t) h] = acc / (double) n;
            }
        }
        return H;
    }

    void writeFrame (WavetableData& t, int frame, const std::vector<std::complex<double>>& H)
    {
        double peak = 0.0;
        for (int L = 0; L < WavetableData::kNumLevels; ++L)
        {
            const int N = t.size[(size_t) L];
            std::vector<std::complex<double>> c ((size_t) N);
            const int hmax = std::min (WavetableData::kMaxHarm[(size_t) L], N / 2 - 1);
            for (int h = 1; h <= hmax && h < (int) H.size(); ++h)
            {
                c[(size_t) h] = H[(size_t) h];
                c[(size_t) (N - h)] = std::conj (H[(size_t) h]);
            }
            fft (c, true);
            float* dst = t.level (frame, L);
            for (int i = 0; i < N; ++i) dst[i] = (float) c[(size_t) i].real();
            dst[N] = dst[0];
            if (L == 0)
                for (int i = 0; i < N; ++i) peak = std::max (peak, std::abs (c[(size_t) i].real()));
        }
        const float g = peak > 1.0e-9 ? (float) (1.0 / peak) : 0.0f;
        for (int L = 0; L < WavetableData::kNumLevels; ++L)
        {
            float* dst = t.level (frame, L);
            for (int i = 0; i <= t.size[(size_t) L]; ++i) dst[i] *= g;
        }
    }

    /** Additive Harmonische mit Sinus-Phase: Amplitude a -> c = -i a / 2 (Grundton phasengleich zu sin) */
    std::vector<std::complex<double>> sineHarmonics (const std::vector<double>& amp, const std::vector<double>* phase = nullptr)
    {
        std::vector<std::complex<double>> H (amp.size());
        for (size_t h = 1; h < amp.size(); ++h)
        {
            const double ph = phase != nullptr ? (*phase)[h] : 0.0;
            H[h] = 0.5 * amp[h] * std::complex<double> (std::sin (ph), -std::cos (ph));   // a/2 * e^{i(ph - π/2)}
        }
        return H;
    }

    std::shared_ptr<WavetableData> makeBuiltin (int index)
    {
        auto t = std::make_shared<WavetableData>();
        t->init (kBuiltinFrames);
        t->name = kBuiltinTableNames[index];
        const int HN = WavetableData::kMaxHarm[0] + 1;

        for (int f = 0; f < kBuiltinFrames; ++f)
        {
            const double x = (double) f / (double) (kBuiltinFrames - 1);
            std::vector<std::complex<double>> H;

            if (index == 1 || index == 2)
            {
                std::vector<double> amp ((size_t) HN, 0.0), ph ((size_t) HN, 0.0);
                uint32_t seed = 0x2545F491u;
                for (int h = 1; h < HN; ++h)
                {
                    const double hd = (double) h;
                    if (index == 1)
                    {
                        // Holz: Formant wandert mit der Position, ungerade Obertöne leicht betont (hohl, "tock")
                        const double c = 2.5 + 18.0 * x;
                        const double bw = 1.0 + 0.15 * c;
                        amp[(size_t) h] = std::pow (hd, -1.1) * (1.0 + 5.0 * std::exp (-std::pow ((hd - c) / bw, 2.0)))
                                          * ((h % 2) == 1 ? 1.0 : 0.55);
                    }
                    else
                    {
                        // Flüstern: Grundton bleibt, darüber ein luftiges Band mit festen Pseudo-Zufallsphasen
                        const double band = 0.25 + 3.0 * std::exp (-std::pow ((std::log2 (hd) - (2.5 + 3.5 * x)) / 0.7, 2.0));
                        amp[(size_t) h] = h == 1 ? 1.0 : band / hd;
                        seed = seed * 1664525u + 1013904223u;
                        ph[(size_t) h] = h == 1 ? 0.0 : 2.0 * kPiD * (double) (seed >> 8) / 16777216.0;
                    }
                }
                H = sineHarmonics (amp, &ph);
            }
            else
            {
                std::vector<double> cyc ((size_t) kCycleN);
                for (int i = 0; i < kCycleN; ++i)
                {
                    const double t01 = (double) i / (double) kCycleN;
                    double y = 0.0;
                    switch (index)
                    {
                        case 0:   // Klassik: Saw -> Square -> schmaler Puls
                        {
                            const double saw = 1.0 - 2.0 * t01, sq = t01 < 0.5 ? 1.0 : -1.0;
                            if (x < 0.5) { const double m = x * 2.0; y = (1.0 - m) * saw + m * sq; }
                            else { const double w = 0.5 - (x - 0.5) * 0.8; y = t01 < w ? 1.0 : -1.0; }
                            break;
                        }
                        case 3:   // Sync: Saw, hart synchronisiert mit Verhältnis 1..8
                        {
                            const double r = 1.0 + 7.0 * x, p = t01 * r;
                            y = 1.0 - 2.0 * (p - std::floor (p));
                            break;
                        }
                        case 4:   // FM-Growl: Phasenmodulation 2:1
                            y = std::sin (2.0 * kPiD * t01 + (0.2 + 5.0 * x) * std::sin (2.0 * kPiD * 2.0 * t01));
                            break;
                        case 5:   // Fold: Sinus-Wavefolder
                        {
                            const double g = 1.0 + 6.0 * x;
                            y = std::sin (g * std::sin (2.0 * kPiD * t01));
                            break;
                        }
                        default: break;
                    }
                    cyc[(size_t) i] = y;
                }
                H = harmonicsOf (cyc);
            }
            writeFrame (*t, f, H);
        }
        t->id = hashFloats (t->data) ^ (uint64_t) (index + 1);
        return t;
    }
}

//==============================================================================
void fft (std::vector<std::complex<double>>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * kPiD / (double) len * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

const std::array<std::shared_ptr<const WavetableData>, kNumBuiltinTables>& builtinWavetables()
{
    static const std::array<std::shared_ptr<const WavetableData>, kNumBuiltinTables> tables = []
    {
        std::array<std::shared_ptr<const WavetableData>, kNumBuiltinTables> t;
        for (int i = 0; i < kNumBuiltinTables; ++i) t[(size_t) i] = makeBuiltin (i);
        return t;
    }();
    return tables;
}

std::shared_ptr<WavetableData> buildWavetableFromCycles (const std::vector<std::vector<double>>& cycles, const std::string& name)
{
    if (cycles.empty()) return nullptr;
    auto t = std::make_shared<WavetableData>();
    t->init ((int) cycles.size());
    t->name = name;
    for (size_t f = 0; f < cycles.size(); ++f)
        writeFrame (*t, (int) f, harmonicsOf (cycles[f]));
    t->id = hashFloats (t->data);
    return t;
}

std::shared_ptr<WavetableData> wavetableFromAudio (const std::vector<float>& mono, double rate,
                                                   const std::string& name, std::string& info)
{
    const int len = (int) mono.size();
    std::vector<std::vector<double>> cycles;

    if (len >= 2048 && len % 2048 == 0)
    {
        const int frames = len / 2048;
        const int use = std::min (frames, 256);
        for (int k = 0; k < use; ++k)
        {
            const int f = use == frames ? k : (int) std::lround ((double) k * (frames - 1) / (double) (use - 1));
            cycles.emplace_back (mono.begin() + f * 2048, mono.begin() + (f + 1) * 2048);
        }
        info = std::to_string (use) + " Frames (Wavetable-Format 2048)";
    }
    else if (len >= 16 && len < 4096)
    {
        cycles.emplace_back (mono.begin(), mono.end());
        info = "Einzelner Zyklus (" + std::to_string (len) + " Samples)";
    }
    else if (len >= 4096)
    {
        // tonhöhensynchron: Grundton im mittleren Bereich erkennen
        const int mid = len / 4;
        const auto p = detectPitch (mono, rate, mid, std::min (len - mid, (int) (0.5 * rate)), 25.0, 1000.0);
        if (p.hz > 0.0 && p.confidence > 0.5)
        {
            const double period = rate / p.hz;
            const int maxCycles = (int) std::floor (((double) len - period - 4.0) / period);
            const int use = std::min (64, std::max (1, maxCycles));
            for (int k = 0; k < use; ++k)
            {
                // Zyklusanfänge als ganze Vielfache der Periode -> gleiche Phasenlage in allen Frames
                const int m = use == 1 ? 0 : (int) std::lround ((double) k * (maxCycles - 1) / (double) (use - 1));
                const double s0 = (double) m * period;
                std::vector<double> cyc ((size_t) kCycleN);
                for (int i = 0; i < kCycleN; ++i)
                {
                    const double pos = s0 + period * (double) i / (double) kCycleN;
                    const int i0 = (int) pos;
                    const double fr = pos - (double) i0;
                    const double a = mono[(size_t) std::min (len - 1, i0)], b = mono[(size_t) std::min (len - 1, i0 + 1)];
                    cyc[(size_t) i] = a + fr * (b - a);
                }
                cycles.push_back (std::move (cyc));
            }
            char buf[96];
            std::snprintf (buf, sizeof buf, "%d Zyklen tonhöhensynchron (Grundton %.1f Hz)", use, p.hz);
            info = buf;
        }
        else
        {
            const int frames = std::min (256, len / 2048);
            for (int k = 0; k < frames; ++k)
                cycles.emplace_back (mono.begin() + k * 2048, mono.begin() + (k + 1) * 2048);
            info = "Kein stabiler Grundton - in 2048er-Frames zerlegt (" + std::to_string (frames) + ")";
        }
    }

    if (cycles.empty())
    {
        info = "Datei zu kurz";
        return nullptr;
    }
    return buildWavetableFromCycles (cycles, name);
}

} // namespace samsi
