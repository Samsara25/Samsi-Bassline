#pragma once

#include "Settings.h"

#include <array>
#include <cstdint>

namespace samsi
{

/** Ein samplegenaues Ereignis innerhalb eines Audioblocks. */
struct EngineEvent
{
    enum Type : uint8_t { AllBassOff = 0, BassOff = 1, KickOn = 2, BassOn = 3 };
    int sample = 0;
    Type type = KickOn;
    int note = 0;        // absolute MIDI-Note (Bass)
    float velocity = 1.0f;
    bool slide = false;
    int source = 0;      // 0 = Sequencer, 1 = MIDI
    int id = 0;
};

/** Feste Kapazität – keine Allokation im Audio-Thread. */
class EventList
{
public:
    static constexpr int kCapacity = 1024;

    void clear() noexcept { count = 0; }
    bool add (const EngineEvent& e) noexcept
    {
        if (count >= kCapacity) return false;
        events[(size_t) count++] = e;
        return true;
    }
    /** Stabile Sortierung nach Sample, bei Gleichstand: Off < Kick < BassOn. */
    void sort() noexcept;
    int size() const noexcept { return count; }
    const EngineEvent& operator[] (int i) const noexcept { return events[(size_t) i]; }

private:
    std::array<EngineEvent, kCapacity> events {};
    int count = 0;
};

/** Zustandsloser Pattern-Sequencer:
    Für einen Block [ppqStart, ppqEnd) werden alle Schritte berechnet, deren
    Ereigniszeit in den Block fällt. Weil nichts "mitgezählt" wird, verhalten sich
    Loop-Sprünge, Transport-Neustarts und Tempoänderungen automatisch korrekt.
    Raster: Sechzehntel (0,25 Viertel). Swing verschiebt ungerade Sechzehntel. */
struct Sequencer
{

    /** bassShiftQn: zeitlicher Versatz der Bass-Lane in Vierteln (Groove + Auto Fit).
        Kick-Zeitpunkte bleiben immer unverändert (Kick-Attack und Groove geschützt). */
    /** Zeitbasis: ppq(sample) = anchorPpq + (sample - anchorSample) / samplesPerQn.
        Ein Ereignis zur Zeit t fällt auf das erste Sample >= t. Da nur ganzzahlige
        Sample-Positionen verglichen werden, ist das Ergebnis unabhängig von der Blockgröße. */
    static void collect (const PatternData& pattern, const GlobalSettings& g,
                         double anchorPpq, int64_t anchorSample, int64_t blockStart, double samplesPerQn,
                         int numSamples, double bassShiftQn, EventList& out) noexcept;

    /** Zeit von Schritt k in Vierteln; Swing wirkt auf ungerade Schritte (nicht im Triolen-Raster). */
    static double stepTimeQn (int64_t k, float swing, int grid) noexcept;
};

} // namespace samsi
