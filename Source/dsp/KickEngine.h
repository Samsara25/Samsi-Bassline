#pragma once

#include "DspUtils.h"
#include "Layers.h"
#include "Settings.h"

namespace samsi
{

/** Kick-Synthese:
      Sinus-Oszillator (double-Phase) mit Pitch-Sweep im log-Frequenzbereich
      fStart -> fEnd, Kurvenform e(x) = (1-x)^curve,
      Amplitudenhüllkurve Attack -> Hold -> Decay (bis Tail-Pegel, Kurve) -> Tail (bis 0),
      Click-Layer: deterministisches Rauschen durch Bandpass mit exponentiellem Abklingen,
      Sättigung: ADAA-tanh.
    Alle Klangwerte werden beim Trigger eingefroren -> jeder Schlag ist reproduzierbar.
    Bei erneutem Trigger wird die alte Stimme in 1,5 ms ausgeblendet (klickfrei). */
class KickEngine
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    /** velocity 0..1, tailScale 0.5..1 (Auto Fit), Werte werden eingefroren.
        smp: optionales Sample für den Sample-Layer (nullptr = keins). */
    void trigger (const KickSettings& s, float velocity, float tailScale, const KickSample* smp = nullptr) noexcept;

    /** Zu Blockbeginn: Sample-Stimmen beenden, deren Sample nicht mehr aktuell ist
        (das alte Sample darf danach freigegeben werden). */
    void validateSample (const KickSample* current) noexcept;

    /** Schreibt n Samples (Mono) nach out (überschreibt): Synth (gesättigt) + Sample-Layer. */
    void render (float* out, int n, const KickSettings& live) noexcept;

    bool isActive() const noexcept { return voices[0].active || voices[1].active || svoices[0].active || svoices[1].active; }

    /** Phase (0..1) und Frequenz des aktuell klingenden Kick-Oszillators – für den
        kausalen "Follow Kick"-Phasenmodus des Basses. */
    double currentPhase() const noexcept   { return voices[current].phase; }
    double currentFreqHz() const noexcept  { return voices[current].lastFreq; }
    bool   currentActive() const noexcept  { return voices[current].active; }

    /** Gesamtlänge eines Schlags in ms (für Analysefenster). */
    static double totalLengthMs (const KickSettings& s, float tailScale) noexcept;

private:
    struct Voice
    {
        bool active = false;
        int64_t t = 0;              // Samples seit Trigger
        double phase = 0.0;
        double lastFreq = 0.0;
        double lastAmp = 0.0, lastPitchExp = 0.0;   // für die Sample-Modulation
        float velocity = 1.0f;
        float fade = 1.0f;          // für Ausblenden bei Retrigger
        float fadeStep = 0.0f;

        // eingefrorene Werte (in Samples)
        double startHz = 400, endHz = 50, logRatio = 0, pitchLen = 1, pitchCurve = 2;
        double attackLen = 1, holdEnd = 1, decayEnd = 1, tailEnd = 1;
        double decayLen = 1, tailLen = 1, decayCurve = 1.5, tailLevel = 0.1;
        double clickGain = 0, clickCoef = 0;
        double clickEnv = 0;
        Lcg noise;
        SvfState clickSvf;
        SvfCoeffs clickCoeffs;

        float tick (double sampleRate) noexcept;
    };

    struct SampleVoice
    {
        bool active = false;
        const KickSample* smp = nullptr;
        int64_t t = 0, delay = 0;
        double pos = 0.0, step = 1.0;
        double holdLen = 1.0, fadeLen = 1.0, fadeIn = 1.0;
        float gain = 1.0f, fade = 1.0f, fadeStep = 0.0f;
        float tick (double rateMul, float envGain) noexcept;
    };

    double sr = 48000.0;
    Voice voices[2];
    SampleVoice svoices[2];
    int scurrent = 0;
    SvfState smpHp1, smpHp2;
    SvfCoeffs smpHpC;
    float smpHpHz = -1.0f;
    int current = 0;
    AdaaTanh shaper;
    LinearSmoother drive;
    uint32_t triggerCount = 0;
    bool primed = false;
};

} // namespace samsi
