#pragma once

#include "OfflineRenderer.h"

#include <array>
#include <vector>

namespace samsi
{

/** Analysebänder im Tieftonbereich. */
constexpr int kNumBands = 3;
inline constexpr const char* kBandNames[kNumBands] = { "20-60 Hz", "60-120 Hz", "120-250 Hz" };

/** Reale Messwerte eines gerenderten Takts. Alle Werte in dB. Kein Gesamt-"Score":
    'cost' ist nur die interne Zielfunktion des Optimierers (kleiner = besser). */
struct FitMetrics
{
    // Auslöschung in Überlappungsfenstern Kick+Bass: 10*log10(E_sum / (E_kick + E_bass)).
    // 0 dB = Leistungsaddition, negativ = Auslöschung, positiv = konstruktiv.
    std::array<double, kNumBands> cancelMeanDb {};   // energiegewichtet, nur Auslöschungsanteil (>= 0)
    std::array<double, kNumBands> cancelWorstDb {};  // schlimmster 10-ms-Frame (>= 0)
    std::array<double, kNumBands> coherenceDb {};    // energiegewichtetes Mittel (vorzeichenbehaftet)

    double sumPeakDb = -120.0;       // Summenpeak (dBFS)
    double kickPeakDb = -120.0, bassPeakDb = -120.0;
    double headroomDb = 120.0;       // = -sumPeakDb
    double lowBumpDb = 0.0;          // Überhöhung des Tiefband-Maximums (20-120 Hz) über Leistungsaddition
    double kickAttackLossDb = 0.0;   // Energieverlust der ersten 8 ms des Kicks durch den Bass
    double onsetCancelDb = 0.0;      // mittlere Auslöschung in den ersten 15 ms der Bassnoten
    double onsetMaskDb = 0.0;        // mittleres Verhältnis Kick/Bass zu Notenbeginn (Maskierung)
    double noteSigmaDb = 0.0;        // Standardabweichung der Bassnoten-Energie
    double bassEnergyDb = -120.0;    // Bass-Energie 20-250 Hz über den Takt
    double kickEnergyDb = -120.0;
    double loudnessLu = -120.0;      // K-gewichtete Lautheit (BS.1770-Filter, ohne Gating) des MIX
    double cost = 0.0;
    // Anteile der Zielfunktion (für Transparenz/Tests): Auslöschung, Peak, Tiefband, Attack,
    // Einsatz-Auslöschung, Maskierung, Gleichmäßigkeit, Basspegel
    static constexpr int kNumTerms = 8;
    std::array<double, kNumTerms> terms {};

    // Detailkurven für die Anzeige
    std::vector<float> lowCoherenceFrames;   // pro 5-ms-Frame, Band 20-120 Hz, dB (NaN = zu leise)
    double frameHopSamples = 0.0;
    std::vector<std::pair<int, int>> overlapWindows;
    std::array<double, kNumBands> kickBandDb {}, bassBandDb {}, sumBandDb {};
};

/** Referenzwerte der Ausgangslage, gegen die Grenzen geprüft werden. */
struct FitReference
{
    double bassEnergyDb = 0.0;
    double noteSigmaDb = 0.0;
    bool valid = false;
};

/** Gewichte der Zielfunktion – bewusst offen dokumentiert (siehe README). */
struct FitWeights
{
    std::array<double, kNumBands> band { 1.0, 0.7, 0.35 };
    double worstFactor  = 0.15;
    double peak         = 2.0;    double peakAllowanceDb = 2.0;
    double lowBump      = 0.5;    double lowBumpAllowanceDb = 1.5;
    double attack       = 3.0;
    double onsetCancel  = 1.5;
    double onsetMask    = 0.25;   double onsetMaskAllowDb = 9.0;
    double evenness     = 0.4;
    double bassLevel    = 8.0;    double bassLossAllowDb = 1.0;  double bassLossRejectDb = 4.0;
};

inline constexpr const char* kTermNames[FitMetrics::kNumTerms] = {
    "Auslöschung", "Summenpeak", "Tiefband-Überhöhung", "Kick-Attack",
    "Einsatz-Auslöschung", "Einsatz-Maskierung", "Gleichmäßigkeit", "Basspegel" };

FitMetrics analyze (const RenderResult& r, const FitReference& ref, const FitWeights& w = {}, bool detail = false);

/** Zusätzliche "möglichst geringe Veränderung"-Kosten eines Fit-Kandidaten. */
double changeCost (const FitSettings& f, float userPhaseDeg) noexcept;

} // namespace samsi
