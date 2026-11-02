#pragma once

#include "BassEngine.h"
#include "KickEngine.h"
#include "Layers.h"
#include "Sequencer.h"
#include "Glue.h"

#include <limits>
#include <vector>

namespace samsi
{

struct TransportInfo
{
    bool   playing = false;
    bool   hasPpq  = false;
    double ppq     = 0.0;
    double bpm     = 145.0;
};

struct MidiEvt
{
    int  sample = 0;
    bool noteOn = true;
    int  note = 60;
    int  velocity = 100;   // 0..127
};

/** Protokoll der ausgelösten Ereignisse (nur für Offline-Analyse, sonst nullptr). */
struct TriggerLog
{
    std::vector<int64_t> kickOn;     // absolute Sample-Positionen
    std::vector<int64_t> bassOn;
    std::vector<int64_t> bassOff;
    std::vector<int> bassNotes;      // MIDI-Note je bassOn
};

/** Gemeinsame, samplegenaue Zeitbasis für Kick und Bass.
    Liefert drei Signale: KICK (stereo, mono-identisch), BASS (stereo) und
    MIX = KICK + BASS (+ optionalem, separat gekennzeichnetem Bus-Clipper).
    Wird identisch vom Plugin und vom Offline-Renderer (Auto Fit) verwendet. */
class SamsiEngine
{
public:
    static constexpr int kMaxMidiPerBlock = 512;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void process (const Settings& s, const PatternData& pattern, const TransportInfo& t,
                  const MidiEvt* midi, int numMidi, int numSamples, const Layers& layers = {}) noexcept;

    /** Lookahead in Samples für eine Einstellung (gleich der gemeldeten Latenz). */
    static int lookaheadSamples (const Settings& s, double sampleRate) noexcept;

    bool isGateOpen() const noexcept { return numGateHeld > 0; }

    // Ausgänge des letzten process()-Aufrufs (Länge = numSamples)
    const float* kick()  const noexcept { return kickBuf.data(); }   // Mono (für L und R)
    const float* bassL() const noexcept { return bassLBuf.data(); }
    const float* bassR() const noexcept { return bassRBuf.data(); }
    const float* mixL()  const noexcept { return mixLBuf.data(); }
    const float* mixR()  const noexcept { return mixRBuf.data(); }

    int  currentStep() const noexcept { return lastStep; }
    double getSampleRate() const noexcept { return sr; }
    int64_t getSampleClock() const noexcept { return clock; }

    void setTriggerLog (TriggerLog* log) noexcept { triggerLog = log; }

    /** Ereignis-Markierungen des letzten process()-Aufrufs je Sample:
        Bit 0 = Kick ausgelöst, Bit 1 = neue Bassnote, Bit 2 = Slide-Note (für Live-Anzeigen). */
    const uint8_t* triggerFlags() const noexcept { return flagBuf.data(); }
    /** Musikalische Position (Viertel) des Samples i im letzten process()-Aufruf,
        bezogen auf den Ausgang (Lookahead berücksichtigt). NaN, wenn keine Zeitleiste läuft. */
    double ppqAt (int i) const noexcept
    {
        if (! tlActive) return std::numeric_limits<double>::quiet_NaN();
        return tlAnchorPpq + (double) (tlChunkClock + i - tlAnchorSample) / tlSpq;
    }

    /** Glue-Anzeige (Kompressor-Reduktion, dyn. EQ-Bänder) */
    const KickBassGlue& getGlue() const noexcept { return glue; }
    KickBassGlue& getGlueMutable() noexcept { return glue; }

    /** Verlust- und Fehlerzähler für Tests */
    int nonFiniteResets() const noexcept { return nanResets; }

private:
    void processChunk (const Settings& s, const PatternData& pattern, const TransportInfo& t,
                       const MidiEvt* midi, int numMidi, int offset, int n) noexcept;
    void handleMidiBass (bool on, int note, int vel, int sample, double glide) noexcept;
    void applyEvent (const EngineEvent& e, const Settings& s, const KickSettings& kEff,
                     const BassSettings& bEff, float tailScale) noexcept;

    double sr = 48000.0;
    int maxBlock = 512;
    int64_t clock = 0;

    KickEngine kickEngine;
    BassEngine bassEngine;
    EventList events;

    std::vector<float> kickBuf, bassLBuf, bassRBuf, mixLBuf, mixRBuf;
    std::vector<uint8_t> flagBuf;
    bool tlActive = false;
    double tlAnchorPpq = 0.0, tlSpq = 1.0;
    int64_t tlAnchorSample = 0, tlChunkClock = 0;

    // Transport-Zustand
    bool wasPlaying = false;
    double anchorPpq = 0.0, anchorBpm = 0.0;
    int64_t anchorSample = 0;
    int lastStep = -1;

    // MIDI: Notenstapel (letzte Note hat Vorrang) und Verzögerungs-Warteschlange
    std::array<int, 16> heldNotes {};
    int numHeld = 0;
    int lastMidiVel = 100;
    struct Pending { int64_t due; bool on; int note; int vel; };
    std::array<Pending, 1024> pending {};
    int numPending = 0;

    // MIDI-Gate-Modus
    std::array<int, 16> gateHeld {};
    int numGateHeld = 0;
    int gateRoot = -1;
    bool internalClock = false;
    struct GateChange { int sample; bool open; int root; };
    std::array<GateChange, 128> gateChanges {};
    int numGateChanges = 0;
    EventList seqEvents;
    const Layers* layers = nullptr;

    GainSmoother kickLevel, master, abGain;
    LinearSmoother busDrive;
    AdaaTanh busClipL, busClipR;

    KickBassGlue glue;
    TriggerLog* triggerLog = nullptr;
    int nanResets = 0;
    bool primed = false;   // Glätter beim ersten Block direkt auf Zielwerte setzen
};

/** Wirksame Bass-Einstellungen inkl. Fit-Anteilen (Attack-Verlängerung). */
BassSettings effectiveBass (const Settings& s) noexcept;
/** Kick-Tail-Skalierung (1 = unverändert). */
float effectiveKickTail (const Settings& s) noexcept;
/** Bass-Versatz in ms (Groove + Fit). */
double bassShiftMs (const Settings& s) noexcept;

} // namespace samsi
