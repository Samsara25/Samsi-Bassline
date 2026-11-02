#pragma once

#include "../dsp/Layers.h"
#include "../dsp/Settings.h"

namespace samsi
{

/** Ergebnis der automatischen Kick-Sample-Ausrichtung. */
struct KickAlignResult
{
    bool valid = false;
    float startMs = 0.0f;     // neuer Sample-Startpunkt
    float delayMs = 0.0f;     // neue Verzögerung
    bool invert = false;      // Polarität
    double coherenceBefore = 0.0, coherenceAfter = 0.0;   // normierte Korrelation im Tiefband (-1..1)
};

/** Richtet das Kick-Sample im Tiefband (< 200 Hz, erste 120 ms) an der Synth-Kick aus:
    sucht Zeitversatz (±10 ms, über Start bzw. Verzögerung) und Polarität mit maximaler
    Korrelation -> die Schichten verstärken sich statt sich auszulöschen.
    Deterministisch, läuft außerhalb des Audio-Threads. */
KickAlignResult alignKickSample (const KickSettings& k, const KickSample& smp, double sampleRate);

} // namespace samsi
