#pragma once

#include "DspUtils.h"
#include "Layers.h"
#include "Settings.h"

#include <vector>

namespace samsi
{

/** Monophoner Psytrance-Bass:
      Oszillatoren: fallender Saw + Pulse (PolyBLEP, bandbegrenzt) + Sinus-Sub,
        alle phasengleich zur Grundwelle; Phasenreset pro Note (Präzisionsmodus)
        mit einstellbarer Startphase.
      Filter: TPT-SVF Tiefpass 12 oder 24 dB/Okt mit Resonanz, Filterhüllkurve,
        Keytracking, Velocity.
      Drive: ADAA-tanh.
      Amp: lineares Attack, exponentielles Decay/Release (schnell, klickfrei).
      Danach (Engine-Ebene): Polarität, Pegelkorrektur, Ducking (optional nur im
      Tiefbandbereich), Pegel, Stereo-Breite nur oberhalb einer Grenzfrequenz
      (Mono-Summe bleibt exakt unverändert).
    Bei einer neuen (nicht gebundenen) Note wird die alte Stimme in 2 ms
    ausgeblendet; dadurch erzeugt ein Phasenreset keinen Knack. */
class BassEngine
{
public:
    enum Source { Sequencer = 0, Midi = 1 };

    void prepare (double sampleRate) ;
    void reset() noexcept;

    /** startPhase01: Oszillator-Startphase (0..1) für diese Note. */
    void noteOn (int midiNote, float velocity, bool slide, int source, int id,
                 double startPhase01, const BassSettings& s) noexcept;
    void noteOff (int source, int id) noexcept;
    void allNotesOff() noexcept;

    /** Tiefe in dB (>=0) und Dauer in ms werden beim Trigger eingefroren. */
    void triggerDuck (float depthDb, float timeMs) noexcept;

    /** Rendert Stereo (überschreibt). wt: Wavetable für den WT-Oszillator (nullptr = keine). */
    void render (float* left, float* right, int n, const BassSettings& live, const FitSettings& fit,
                 const WavetableData* wt = nullptr, const KickSample* smp = nullptr) noexcept;

    bool isActive() const noexcept   { return voices[0].active || voices[1].active; }
    int  currentSource() const noexcept { return voices[current].source; }
    int  currentId() const noexcept     { return voices[current].id; }
    bool currentGate() const noexcept   { return voices[current].active && voices[current].gate; }

private:
    enum class Stage { Off, Attack, Decay, Release };

    struct Env
    {
        Stage stage = Stage::Off;
        double level = 0.0, attackInc = 1.0, decayCoef = 0.0, sustain = 0.0, releaseCoef = 0.0;
        void start (double attackSamples, double decaySamples, double sus, double releaseSamples) noexcept;
        void setTimes (double decaySamples, double sus, double releaseSamples) noexcept;
        void gateOff() noexcept { if (stage != Stage::Off) stage = Stage::Release; }
        double tick() noexcept;
    };

    struct Voice
    {
        bool active = false, gate = false;
        int source = 0, id = -1, note = 48;
        double phase = 0.0, subPhase = 0.0;
        double logFreq = 0.0, targetLogFreq = 0.0;
        float velocity = 1.0f;
        float fade = 1.0f, fadeStep = 0.0f;
        Env amp, fenv;
        SvfState f1, f2;
        SvfCoeffs c1, c2;
        int ctrlCounter = 0;
        double resoComp = 1.0;
        double lastFEnv = 0.0;
        int wtL = 0;
        int64_t pickT = 0;          // Samples seit Notenbeginn (Pick-Kammfilter)
        int64_t ctrlTicks = 0;
        int pickWrite = 0;
        double dampState = 0.0, dampCoef = 1.0;
        double startPhase01 = 0.0;   // für den Sample-Startpunkt
        double smpPos = -1.0;        // < 0: wird beim ersten Sample gesetzt
        AdaaTanh shaper;
    };

    double renderVoice (Voice& v, float* pickBuf, const BassSettings& s, double glideCoef, double drive,
                        const WavetableData* wt, const KickSample* smp) noexcept;
    static constexpr int kPickBufSize = 8192;
    std::vector<float> pickBuffers[2];          // je Stimme, in prepare() angelegt

    double sr = 48000.0;
    Voice voices[2];
    int current = 0;

    // Engine-Nachbearbeitung
    LinearSmoother polarity, drive, width;
    GainSmoother trim, level;
    SvfState duckLp, widthHp;
    SvfCoeffs duckLpC, widthHpC;
    float duckLpHz = -1.0f, widthHpHz = -1.0f;
    int64_t duckT = -1;
    double duckDepthDb = 0.0, duckLen = 1.0, duckRise = 1.0;

    bool primed = false;
    std::vector<float> delayLine;
    int delayWrite = 0, delaySamples = 1;
};

} // namespace samsi
