#pragma once

#include "LiveTap.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace samsi
{

/** Lautheit nach ITU-R BS.1770-4 / EBU R128 (K-Gewichtung, Momentary 400 ms, Short-Term 3 s,
    Integrated mit absolutem -70-LUFS- und relativem -10-LU-Gate). Stereo, Kanalgewichte 1. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (const float* l, const float* r, int n) noexcept;
    double momentaryLufs() const noexcept;
    double shortTermLufs() const noexcept;
    double integratedLufs() const;

private:
    struct Bq { double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
        inline double tick (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    std::array<Bq, 2> pre {}, rlb {};
    int hop = 4800, inHop = 0;
    double acc = 0.0;
    std::array<double, 30> blocks {};   // mittlere Leistung je 100 ms (Summe über Kanäle)
    int nBlocks = 0, head = 0;
    std::vector<double> gatingBlocks;   // 400-ms-Blöcke (alle 100 ms) für Integrated
};

/** 4-fach-Überabtastung für True Peak (Polyphasen-FIR, 48 Abgriffe, Kaiser-Fenster). */
class TruePeak
{
public:
    TruePeak();
    void reset() noexcept { std::fill (hist.begin(), hist.end(), 0.0f); pos = 0; }
    float process (float x) noexcept;   // liefert max |Zwischenwert| für dieses Sample
private:
    static constexpr int kTaps = 12;
    std::array<std::array<float, kTaps>, 4> h {};
    std::array<float, kTaps> hist {};
    int pos = 0;
};

//==============================================================================
/** Auswertung der Live-Daten auf dem Oberflächen-Thread (nie im Audio-Thread).
    Hält einen Verlauf der letzten ~2,7 s (48 kHz) aller Kanäle und berechnet daraus
    Pegel, Lautheit, Korrelation, Beat-synchrone Wellenformen, Live-Mitschnitte von Kick
    und Bass, Spektren und das Sidechain-Tempo. */
class LiveAnalyzer
{
public:
    static constexpr int kHistory = 1 << 17;
    static constexpr int kBeatBins = 2048;
    static constexpr int kCaptures = 8;
    enum Trace { MixL, MixR, Kick, Bass, ScL, ScR, NumTraces };

    void prepare (double sampleRate);
    double getSampleRate() const noexcept { return sr; }

    /** Neue Daten aus dem Tap; gibt die Anzahl verarbeiteter Werte zurück. */
    int drain (LiveTap& tap);
    void feed (const TapFrame* f, int n);
    void setFrozen (bool f) noexcept { frozen = f; }
    bool isFrozen() const noexcept { return frozen; }
    void resetMeters();

    //--- Verlauf --------------------------------------------------------------
    /** Wert des Kanals vor 'ago' Samples (0 = neuester). */
    float hist (int trace, int ago) const noexcept
    {
        return history[(size_t) trace][(size_t) ((writePos - 1 - ago) & (kHistory - 1))];
    }
    uint32_t histFlags (int ago) const noexcept { return flagHist[(size_t) ((writePos - 1 - ago) & (kHistory - 1))]; }
    int64_t totalSamples() const noexcept { return total; }
    /** Signal-Trigger: Abstand (in Samples zurück) des jüngsten Kick-Starts bzw. der jüngsten
        steigenden Flanke, vor dem noch 'window' Samples liegen. -1 = keiner. */
    int findTrigger (int trace, int window) const noexcept;

    //--- Beat-Modus (ein Viertel bei Host-Tempo) ----------------------------------
    const std::vector<float>& beatTrace (int trace) const noexcept { return beat[(size_t) trace]; }
    bool beatValid() const noexcept { return beatAge < (int) (sr * 0.5); }

    //--- Live-Mitschnitte (jeder Kick / jede Bassnote) ------------------------------
    struct Capture
    {
        std::vector<float> data;
        int length = 0;          // gültige Samples
        float peakDb = -120.0f;
        float lengthMs = 0.0f;   // Kick: bis -20 dB; Bass: bis -20 dB oder nächste Note
        float pitchHz = 0.0f;    // Nulldurchgangs-Schätzung (Kick: Ausklang, Bass: Notenmitte)
        int64_t serial = 0;
        bool slide = false;
    };
    /** i = 0: neuester; finishedOnly = false liefert auch den gerade laufenden Mitschnitt
        (length wächst). nullptr, wenn (noch) keiner vorhanden. */
    const Capture* kickCapture (int i, bool finishedOnly = true) const noexcept;
    const Capture* bassCapture (int i, bool finishedOnly = true) const noexcept;
    int kickCaptureLen() const noexcept { return capLenKick; }
    int bassCaptureLen() const noexcept { return capLenBass; }

    //--- Pegel ------------------------------------------------------------------------
    struct Levels
    {
        float peakL = 0, peakR = 0;           // Sample-Peak (Halten 1,5 s), linear
        float truePeakDb = -120.0f;            // dBTP (Halten bis Reset)
        float samplePeakDb = -120.0f;          // höchster Sample-Peak seit Reset
        float rmsL = 0, rmsR = 0;              // 300 ms, linear
        float rmsDb = -120.0f;                 // L+R
        float crestDb = 0.0f;
        double lufsM = -120, lufsS = -120, lufsI = -120;
        float correlation = 0.0f;              // -1..+1
        float widthPct = 0.0f;
        float balance = 0.0f;                  // -1 links .. +1 rechts
        float scPeak = 0.0f;
    };
    const Levels& levels() const noexcept { return lv; }

    //--- Spektrum -----------------------------------------------------------------
    enum class Window { Hann, Hamming, BlackmanHarris, Flattop, Rectangular };
    /** Betrag in dBFS (Sinus mit Amplitude A ergibt 20*log10(A)) je FFT-Bin.
        source: Trace oder -1 = Mix (L+R)/2, -2 = Sidechain (L+R)/2 */
    void computeSpectrum (int source, int fftOrder, Window w, std::vector<float>& outDb);

    //--- Sidechain-Tempo ---------------------------------------------------------------
    /** Tempo des Sidechain-Signals (BPM), 0 = kein gleichmäßiger Beat erkannt. */
    double sidechainTempo() const noexcept { return scTempo; }
    double sidechainTempoConfidence() const noexcept { return scTempoConf; }
    /** Tempo-Schätzung aus einem Mono-Signal (für Tests auch direkt nutzbar). */
    static double estimateTempo (const std::vector<float>& x, double sampleRate, double& confidence);
    /** wie oben, aus einer Onset-Kurve (positive Pegelsprünge je Hop) */
    static double estimateTempoFromFlux (std::vector<double> flux, double hopsPerSec, double& confidence);

private:
    void startCapture (std::array<Capture, kCaptures>& caps, int& head, int len, bool slide);
    void finishCapture (Capture& c, bool isKick);
    void updateTempo();

    double sr = 48000.0;
    bool frozen = false;
    std::array<std::vector<float>, NumTraces> history;
    std::vector<uint32_t> flagHist;
    int writePos = 0;
    int64_t total = 0;

    std::array<std::vector<float>, NumTraces> beat;
    int beatAge = 1 << 30;

    std::array<Capture, kCaptures> kickCaps, bassCaps;
    int kickHead = -1, bassHead = -1;       // Index des neuesten Mitschnitts
    int kickFill = 0, bassFill = 0;         // noch zu füllende Samples des neuesten
    int capLenKick = 0, capLenBass = 0;
    int64_t serial = 0;

    Levels lv;
    LoudnessMeter loud;
    TruePeak tpL, tpR;
    double msL = 0, msR = 0, sxy = 0, sxx = 0, syy = 0, eM = 0, eS = 0, rmsCoef = 0.0;
    float holdPeakL = 0, holdPeakR = 0;
    int holdL = 0, holdR = 0, recentPeakAge = 0;
    float recentPeak = 0.0f;

    std::unique_ptr<juce::dsp::FFT> fft;
    int fftOrderNow = 0;
    std::vector<float> fftBuf, win;
    Window winNow = Window::Hann;
    float winSum = 1.0f;

    int64_t lastTempoAt = 0;
    // Sidechain-Onsets: Energie je 2,5-ms-Hop, letzte 12 s
    int scHop = 120, scInHop = 0;
    double scAcc = 0.0, scPrevE = 0.0;
    std::vector<double> scFlux;
    int scFluxPos = 0, scFluxCount = 0;
    double scTempo = 0.0, scTempoConf = 0.0;
};

} // namespace samsi
