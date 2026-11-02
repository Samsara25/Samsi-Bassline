#pragma once

#include <vector>

namespace samsi
{

struct PitchResult
{
    double hz = 0.0;          // 0 = nicht erkannt
    double confidence = 0.0;  // 0..1 (Höhe des normierten Autokorrelationsmaximums)
};

/** Grundton-Erkennung nach dem NSDF-Verfahren (McLeod & Wyvill 2005) auf einem
    tiefpassgefilterten, heruntergetakteten Ausschnitt.
    start/length in Samples; Suchbereich minHz..maxHz. */
PitchResult detectPitch (const std::vector<float>& x, double rate, int start, int length,
                         double minHz = 25.0, double maxHz = 400.0);

/** Kick: analysiert den abklingenden Teil nach dem Pegelmaximum (der Pitch-Sweep ist dann
    weitgehend vorbei) im Bereich 25..200 Hz. */
PitchResult detectKickPitch (const std::vector<float>& x, double rate);

} // namespace samsi
