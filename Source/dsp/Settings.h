#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace samsi
{

//==============================================================================
/** Alle Klang- und Steuerwerte in natürlichen Einheiten (Hz, ms, dB, 0..1).
    Wird pro Audioblock aus den Host-Parametern gelesen und ebenso vom
    Offline-Renderer (Auto Fit, Vorschau) benutzt – dadurch klingt die Analyse
    exakt wie das Plugin. Reines POD, keine Allokation. */
struct KickSettings
{
    float levelDb      = -4.0f;
    float startHz      = 480.0f;
    float endHz        = 46.25f;   // Grundton (F#1)
    float pitchMs      = 45.0f;
    float pitchCurve   = 2.5f;     // >1 = schneller Anfangsabfall (log-Frequenz)
    float attackMs     = 0.3f;
    float holdMs       = 25.0f;
    float decayMs      = 220.0f;
    float decayCurve   = 1.6f;
    float tailLevel    = 0.15f;    // 0..1
    float tailMs       = 120.0f;
    float startPhaseDeg = 0.0f;
    float clickLevelDb = -16.0f;
    float clickDecayMs = 4.0f;
    float clickToneHz  = 3500.0f;
    float driveDb      = 4.0f;

    // Sample-Layer
    bool  smpOn        = false;
    float smpLevelDb   = 0.0f;
    float smpTuneSt    = 0.0f;     // Halbtöne
    float smpOffsetMs  = 0.0f;     // Verzögerung gegenüber dem Kick-Trigger
    float smpStartMs   = 0.0f;     // Startpunkt im Sample
    float smpLengthMs  = 400.0f;   // Haltezeit
    float smpFadeMs    = 60.0f;    // Ausblendzeit danach
    float smpLowCutHz  = 20.0f;    // Hochpass nur auf dem Sample (20 = aus)
    bool  smpInvert    = false;    // Polarität des Samples (automatische Ausrichtung)
    // Modulation des Samples durch die Kick-Regler
    float smpAmpEnv    = 0.0f;     // 0..1: Lautstärke-Hüllkurve der Kick (Attack/Hold/Decay/Tail) auf das Sample
    float smpPitchEnv  = 0.0f;     // 0..1: Pitch-Hüllkurve der Kick (Start -> Grundton) auf die Sample-Tonhöhe
    bool  smpKeyTrack  = false;    // Sample-Grundton folgt dem Kick-Grundton (erkannter Grundton nötig)
    bool  smpDrive     = false;    // Sample durch die Kick-Sättigung
};

enum class FilterMode : int { LP12 = 0, LP24 = 1 };

struct BassSettings
{
    float levelDb     = -8.0f;
    float saw         = 0.8f;
    float square      = 0.0f;
    float pulseWidth  = 0.5f;
    float sub         = 0.5f;
    int   subOctave   = 0;       // 0 oder -1
    float startPhaseDeg = 0.0f;
    bool  phaseReset  = true;    // Präzisionsmodus: reproduzierbarer Phasenreset pro Note
    float cutoffHz    = 900.0f;
    float resonance   = 0.25f;
    FilterMode filterMode = FilterMode::LP24;
    float envAmountOct = 2.5f;
    float keytrack    = 0.5f;
    float fAttackMs   = 0.3f;
    float fDecayMs    = 90.0f;
    float fSustain    = 0.0f;
    float fReleaseMs  = 20.0f;
    float attackMs    = 0.5f;
    float decayMs     = 140.0f;
    float sustain     = 0.6f;
    float releaseMs   = 8.0f;
    float velToAmp    = 0.3f;
    float velToFilter = 0.4f;
    float driveDb     = 3.0f;
    float glideMs     = 40.0f;
    float width       = 0.0f;
    float widthHz     = 250.0f;
    float grooveDelayMs = 0.0f;  // manueller Groove-Versatz der Bass-Lane

    // Wavetable-Oszillator (phasengleich mit dem klassischen Oszillator)
    float wtLevel     = 0.0f;
    int   wtTable     = 0;       // 0..5 eingebaut, 6 = eigene Datei
    float wtPos       = 0.0f;    // Morph-Position 0..1
    float wtEnvAmt    = 0.0f;    // Filter-Hüllkurve -> Position (-1..1)

    // Pick (inspiriert vom Konzept virtueller Zupfbässe: Anschlagposition + Dämpfer)
    float pickAmount  = 0.0f;    // 0 = aus
    float pickPos     = 0.2f;    // Anschlagposition 0,05..0,5 (Kammfilter-Kerben)
    float damper      = 0.0f;    // 0 = offen, 1 = stark gedämpft (Höhen klingen schneller ab)

    // Bass-Sample als Oszillator: wird auf jede Note gestimmt, Startphase/Auto Fit wirken mit
    float smpLevel    = 0.0f;
    float smpTuneSt   = 0.0f;
    float smpStartMs  = 0.0f;
    bool  smpThru     = true;    // Sample durch Pick, Dämpfer, Filter und Drive (sonst nur Amp-Hüllkurve)
    float smpPitchEnvSt = 0.0f;  // -24..24 Halbtöne: Filter-Hüllkurve moduliert die Sample-Tonhöhe
};

enum class PhaseMode : int { Fixed = 0, FollowKick = 1 };

/** Ergebnisse von Auto Fit – als normale, automatisierbare Parameter gespeichert,
    damit der Nutzer sie manuell übersteuern kann. */
struct FitSettings
{
    bool  enabled       = true;   // A/B: false = "vorher"
    float phaseDeg      = 0.0f;   // Oszillator-Startphase des Basses (zusätzlich zur Nutzerphase)
    bool  polarity      = false;  // Polaritätsumkehr des gesamten Bass-Signals
    float offsetMs      = 0.0f;   // zeitlicher Versatz der Bassnoten
    float duckDb        = 0.0f;   // Ducking-Tiefe
    float duckMs        = 80.0f;
    bool  duckLowOnly   = true;   // frequenzselektives Ducking
    float duckHz        = 150.0f;
    float trimDb        = 0.0f;   // begrenzte Pegelkorrektur (±3 dB)
    float attackAddMs   = 0.0f;   // Verlängerung des Bass-Attacks
    float kickTailPct   = 100.0f; // Kick-Tail-Skalierung (nur wenn Kick nicht gesperrt)
    PhaseMode phaseMode = PhaseMode::Fixed;
};

struct FitConstraints
{
    bool  lockKick     = true;
    bool  lockTiming   = true;
    bool  lockBassTone = false;
    float maxShiftMs   = 1.5f;
};

struct GlobalSettings
{
    bool  seqOn        = true;
    float swing        = 0.0f;   // 0..0.6
    int   rootNote     = 30;     // MIDI-Notennummer (F#1 in wissenschaftlicher Notation)
    int   midiKickNote = 36;     // C1 in Ableton-Notation
    float masterDb     = 0.0f;
    bool  busClipOn    = false;  // gemeinsamer, nichtlinearer MIX-Bus-Effekt
    float busClipDriveDb = 0.0f;
    bool  loudMatch    = true;
    int   monitor      = 0;      // nur MIX-Ausgang: 0 Summe, 1 nur Kick, 2 nur Bass (Solo)
    float abGainDb     = 0.0f;   // Lautheitsausgleich für "vorher" im A/B-Vergleich (aus Analyse)
    int   seqMode      = 0;
    int   seqGrid      = 0;      // 0 = 1/16, 1 = 1/16-Triole, 2 = 1/32      // 0 Transport, 1 MIDI-Gate, 2 MIDI-Gate + Transponieren
    int   lookaheadMs  = 0;      // 0 = aus, sonst Vorausschau für Live-MIDI (meldet Latenz)

    // Tonart (nur Analyse/Anzeige, beeinflusst keinen Klang)
    int   keyRoot      = 6;      // 0 = C ... 11 = H/B
    int   keyScale     = 1;      // siehe KeyAnalysis
    float keyTolCents  = 15.0f;
};

/** Ausgangs-Utility (gleiche Kette auf MIX, KICK und BASS -> MIX = KICK + BASS bleibt erhalten).
    Alle Stufen außer dem Limiter sind linear. Der Limiter wirkt nur auf MIX und ist als
    nichtlinear gekennzeichnet (wie der Bus-Clipper). */
struct UtilitySettings
{
    bool  on          = true;
    bool  post        = true;    // Messung nach (true) oder vor (false) der Utility
    bool  invertL     = false, invertR = false;
    bool  swap        = false;
    int   listen      = 0;       // 0 ST, 1 L, 2 R, 3 M, 4 S, 5 L dual, 6 R dual
    bool  dcBlock     = false;
    bool  monoOn      = false;   // Seitensignal unterhalb monoHz entfernen
    float monoHz      = 120.0f;
    float imager      = 1.0f;    // 0..4 (Seitensignal-Faktor)
    float tiltMs      = 0.0f;    // -20..20: >0 R später, <0 L später
    bool  phaseOn     = false;
    float phaseDeg    = 0.0f;    // 0..360
    bool  timeShiftOn = false;   // meldet 50 ms Grundlatenz -> negative Werte möglich
    float timeShiftMs = 0.0f;    // -50..50
    float mix         = 1.0f;    // 0..1, gleichleistungs-Überblendung
    float outLDb      = 0.0f, outRDb = 0.0f;
    bool  outLink     = true;
    bool  limiterOn   = false;   // nur MIX, nichtlinear
    float limiterCeilDb = -0.2f;

    /** true, wenn die lineare Kette exakt durchlässt (dann wird sie übersprungen). */
    bool isIdentity() const noexcept
    {
        return ! on || (! invertL && ! invertR && ! swap && listen == 0 && ! dcBlock && ! monoOn && imager == 1.0f
                        && tiltMs == 0.0f && ! phaseOn && ! timeShiftOn && mix >= 1.0f && outLDb == 0.0f
                        && (outLink || outRDb == 0.0f));
    }
};

/** Kick→Bass-"Glue": Sidechain-Kompressor und dynamischer EQ auf dem Bass, gesteuert vom
    Kick-Signal derselben Instanz (sample-genau, keine Latenz). Wirkt nur auf den Bass – deshalb
    bleibt MIX = KICK + BASS erhalten; Auto Fit misst den Klang inklusive Glue. */
struct DynBandSettings
{
    bool  on       = false;
    int   type     = 0;        // 0 Bell, 1 Low-Shelf
    float freqHz   = 55.0f;
    float q        = 1.0f;
    float rangeDb  = -6.0f;    // Ziel-Änderung bei voller Auslösung (negativ = absenken)
    float threshDb = -30.0f;   // Pegel des Kick-Anteils im Band
    float attackMs = 1.0f;
    float releaseMs = 60.0f;
};

struct GlueSettings
{
    bool  on        = false;
    // Sidechain-Kompressor (Schlüssel = Kick)
    bool  compOn    = true;
    int   style     = 0;       // 0 Clean (Pegel-Hüllkurve in dB), 1 Pumping (lineare, weichere Rückkehr)
    float threshDb  = -24.0f;
    float ratio     = 3.0f;
    float attackMs  = 1.0f;
    float releaseMs = 80.0f;
    float kneeDb    = 6.0f;
    float rangeDb   = 6.0f;    // maximale Pegelreduktion
    float scHpHz    = 20.0f;   // Sidechain-Filter auf dem Kick-Schlüssel
    float scLpHz    = 300.0f;
    float mix       = 1.0f;
    DynBandSettings band[2];
};

struct Settings
{
    UtilitySettings utility;
    GlueSettings   glue;
    KickSettings   kick;
    BassSettings   bass;
    FitSettings    fit;
    FitConstraints constraints;
    GlobalSettings global;
};

//==============================================================================
/** Ein Sequencer-Schritt, in 32 Bit gepackt, damit GUI und Audio-Thread
    ohne Sperren darauf zugreifen können. */
struct Step
{
    bool kick = false;
    int  kickVel = 110;  // 1..127
    bool bass = false;
    int  bassVel = 100;  // 1..127
    int  note = 0;       // Halbtöne relativ zum Grundton, -24..+24
    int  lenPct = 75;    // Notenlänge in % eines Sechzehntels, 10..400
    bool slide = false;

    uint32_t pack() const noexcept;
    static Step unpack (uint32_t v) noexcept;
};

/** Maximale Schrittzahl eines Takts (1/32-Raster). Aktiv sind je nach Raster 16, 24 oder 32. */
constexpr int kNumSteps = 32;

/** Raster: 0 = 1/16 (16 Schritte/Takt), 1 = 1/16-Triole (24), 2 = 1/32 (32). */
inline int stepsForGrid (int grid) noexcept       { return grid == 1 ? 24 : grid == 2 ? 32 : 16; }
inline double stepQnForGrid (int grid) noexcept   { return grid == 1 ? 1.0 / 6.0 : grid == 2 ? 0.125 : 0.25; }

struct PatternData
{
    std::array<uint32_t, kNumSteps> steps {};
    Step get (int i) const noexcept { return Step::unpack (steps[(size_t) i]); }
    void set (int i, const Step& s) noexcept { steps[(size_t) i] = s.pack(); }

    static PatternData makeDefault() noexcept;   // K-B-B-B
    static PatternData makeOffbeat() noexcept;   // K - B B  (Offbeat-Variante)
    /** Rollendes Grundmuster passend zum Raster (0 = 1/16, 1 = Triole, 2 = 1/32):
        offbeat = false: K-B-B-B / K-.-B-B-B-B / K-.-.-B-B-B-B-B
        offbeat = true:  K-.-B-B / K-.-B-.-B-B / K-.-.-.-B-B-B-B */
    static PatternData makeRolling (int grid, bool offbeat) noexcept;
    static PatternData makeEmpty() noexcept;
};

/** Thread-sicherer Speicher (lock-free) für das Pattern. */
class PatternStore
{
public:
    PatternStore() { setAll (PatternData::makeDefault()); }

    PatternData snapshot() const noexcept
    {
        PatternData p;
        for (size_t i = 0; i < (size_t) kNumSteps; ++i)
            p.steps[i] = steps[i].load (std::memory_order_relaxed);
        return p;
    }

    void setAll (const PatternData& p) noexcept
    {
        for (size_t i = 0; i < (size_t) kNumSteps; ++i)
            steps[i].store (p.steps[i], std::memory_order_relaxed);
    }

    void setStep (int i, const Step& s) noexcept
    {
        if (i >= 0 && i < kNumSteps)
            steps[(size_t) i].store (s.pack(), std::memory_order_relaxed);
    }

    Step getStep (int i) const noexcept
    {
        return Step::unpack (steps[(size_t) (i & (kNumSteps - 1))].load (std::memory_order_relaxed));
    }

private:
    std::array<std::atomic<uint32_t>, kNumSteps> steps;
};

//==============================================================================
/** Hilfsfunktion: Hash über alle klangrelevanten Werte (ohne Fit-Parameter),
    um veraltete Analysen zu erkennen. */
uint64_t soundHash (const Settings& s, const PatternData& p, double bpm, double sampleRate, uint64_t layerId = 0) noexcept;

} // namespace samsi
