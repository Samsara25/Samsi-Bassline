#pragma once

#include "FitAnalyzer.h"
#include "SampleAlign.h"

#include <atomic>
#include <string>
#include <vector>

namespace samsi
{

struct FitResult
{
    bool ok = false;
    FitSettings best;                 // gefundene Fit-Werte
    FitMetrics before;                // Fit deaktiviert (Ausgangsklang)
    FitMetrics after;                 // mit 'best'
    double abGainDb = 0.0;            // Lautheitsausgleich für "vorher" im A/B
    uint64_t hash = 0;                // Klang-Hash, für den die Analyse gilt
    int evaluations = 0;
    std::vector<std::string> notes;   // verständliche Hinweise / Kompromisse
    KickAlignResult kickAlign;        // automatische Kick-Sample-Ausrichtung (falls Sample aktiv)
};

/** "Analyze & Fit" für das bekannte interne Pattern.
    Läuft in einem Hintergrund-Thread; rendert jeden Kandidaten mit der echten Engine
    (nach Filter, Drive, Ducking) und bewertet ihn mit analyze().
    Suchreihenfolge (koordinatenweise):
      1. Phasenmodus x Polarität x Startphase (15°-Raster, dann 3°-Feinsuche)
      2. Bass-Attack-Verlängerung, dann Ducking (Tiefe x Dauer)   – gesperrt durch "Lock Bass Tone"
      3. Zeitversatz innerhalb ±maxShift (0,25-ms-Raster)           – gesperrt durch "Lock Timing"
      4. Kick-Tail-Kürzung                                          – gesperrt durch "Lock Kick"
      5. Pegelkorrektur ±3 dB
      6. erneute Phasen-Feinsuche */
FitResult runAutoFit (const Settings& s, const PatternData& pattern, double sampleRate, double bpm,
                      const std::atomic<bool>& cancel, std::atomic<float>& progress, const Layers& layers = {});

} // namespace samsi
