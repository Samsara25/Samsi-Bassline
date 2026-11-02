#pragma once

#include "../dsp/Settings.h"

#include <string>
#include <utility>
#include <vector>

namespace samsi
{

/** Werks-Preset: alle nicht genannten Parameter stehen auf ihrem Standardwert.
    Die Stile sind eigene Klangentwürfe, inspiriert von Spielarten des Psytrance –
    keine Kopien von Künstlern, Produkten oder Samples. */
struct FactoryPreset
{
    std::string name;
    std::string theme;                                 // Themengruppe für das Preset-Menü
    std::string hint;                                  // empfohlenes Tempo, Tonart, Charakter
    std::vector<std::pair<const char*, float>> values; // Parameter-ID -> Klarwert
    PatternData pattern;
};

const std::vector<FactoryPreset>& factoryPresets();

} // namespace samsi
