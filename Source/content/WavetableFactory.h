#pragma once

#include "../dsp/Layers.h"

#include <array>
#include <complex>
#include <memory>
#include <string>
#include <vector>

namespace samsi
{

/** Radix-2-FFT (in-place). Größe muss eine Zweierpotenz sein. inverse: ohne 1/N-Normierung. */
void fft (std::vector<std::complex<double>>& a, bool inverse);

/** Namen der eingebauten Tabellen (Index = Parameterwert). */
inline constexpr const char* kBuiltinTableNames[kNumBuiltinTables] = {
    "Klassik (Saw > Square > Pulse)", "Holz (Formant)", "Flüstern (Luft)", "Sync", "FM-Growl", "Fold" };

/** Eingebaute Tabellen (einmal erzeugt, von allen Instanzen geteilt, nie freigegeben). */
const std::array<std::shared_ptr<const WavetableData>, kNumBuiltinTables>& builtinWavetables();

/** Baut eine Tabelle aus Zyklen im Zeitbereich (jeder Zyklus beliebiger Länge >= 16).
    Obertöne werden pro Mip-Stufe abgeschnitten (bandbegrenzt), DC entfernt,
    jeder Frame auf Spitzenpegel 1 normiert. */
std::shared_ptr<WavetableData> buildWavetableFromCycles (const std::vector<std::vector<double>>& cycles, const std::string& name);

/** Wandelt eine Audiodatei (Mono) in eine Wavetable:
      - Länge = Vielfaches von 2048 -> Frames à 2048 (Serum/Vital-Format), max. 256
      - kürzer als 4096 -> ein einziger Zyklus
      - sonst: tonhöhensynchrone Zerlegung (Grundton erkennen, bis 64 Zyklen über die Datei verteilt)
    Gibt nullptr zurück, wenn nichts Brauchbares entsteht. errorOut erklärt warum. */
std::shared_ptr<WavetableData> wavetableFromAudio (const std::vector<float>& mono, double rate,
                                                   const std::string& name, std::string& info);

} // namespace samsi
