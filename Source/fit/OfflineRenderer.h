#pragma once

#include "../dsp/SamsiEngine.h"

#include <vector>

namespace samsi
{

/** Ergebnis eines Offline-Renderings genau eines Takts (16 Sechzehntel) inklusive
    der Ausklänge des vorherigen Takts. Positionen relativ zum Taktanfang. */
struct RenderResult
{
    double sampleRate = 48000.0, bpm = 145.0;
    std::vector<float> kick, bass, mix;        // Mono: Kick, Bass (L+R)/2, Mix (L+R)/2
    std::vector<float> mixL, mixR;             // für Peak / Lautheit
    std::vector<int> kickOn, bassOn, bassOff;  // Samplepositionen im Takt (bassOn kann < 0 sein -> Note aus Vortakt)
    double kickLengthSamples = 0.0;
    int length() const noexcept { return (int) kick.size(); }
};

/** Rendert das interne Pattern mit exakt derselben Engine wie das Plugin.
    Läuft außerhalb des Audio-Threads (darf allozieren). Deterministisch. */
RenderResult renderPattern (const Settings& s, const PatternData& pattern, double sampleRate, double bpm,
                            const Layers& layers = {});

} // namespace samsi
