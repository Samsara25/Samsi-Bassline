#include "KeyAnalysis.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace samsi
{

namespace
{
    // Intervalle (Halbtöne ab Grundton) je Skala
    constexpr std::array<std::array<bool, 12>, kNumScales> kScales {{
        {{ 1,0,1,1,0,1,0,1,1,0,1,0 }},   // Moll (Äolisch)
        {{ 1,1,0,1,0,1,0,1,1,0,1,0 }},   // Phrygisch
        {{ 1,0,1,1,0,1,0,1,1,0,0,1 }},   // Harmonisch Moll
        {{ 1,1,0,0,1,1,0,1,1,0,1,0 }},   // Phrygisch-Dominant
        {{ 1,0,1,0,1,1,0,1,0,1,0,1 }},   // Dur
        {{ 1,0,1,1,0,1,0,1,0,1,1,0 }},   // Dorisch
        {{ 1,1,1,1,1,1,1,1,1,1,1,1 }},   // Chromatisch
    }};

    std::string noteName (int midi)
    {
        static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int oct = midi / 12 - 2;    // Ableton/Cubase/Bitwig-Notation: C3 = 60
        return std::string (names[((midi % 12) + 12) % 12]) + std::to_string (oct);
    }

    std::string fmt (const char* f, double a = 0, double b = 0, double c = 0)
    {
        char buf[256];
        std::snprintf (buf, sizeof buf, f, a, b, c);
        return buf;
    }

    const char* intervalName (int semis)
    {
        static const char* n[12] = { "Prime/Oktave", "kleine Sekunde", "große Sekunde", "kleine Terz", "große Terz", "Quarte",
                                     "Tritonus", "Quinte", "kleine Sexte", "große Sexte", "kleine Septime", "große Septime" };
        return n[((semis % 12) + 12) % 12];
    }
}

bool inScale (int pc, int keyRoot, int scale) noexcept
{
    scale = std::min (kNumScales - 1, std::max (0, scale));
    return kScales[(size_t) scale][(size_t) (((pc - keyRoot) % 12 + 12) % 12)];
}

double midiToHz (double midi) noexcept { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
double hzToMidi (double hz) noexcept   { return 69.0 + 12.0 * std::log2 (std::max (1.0e-3, hz) / 440.0); }

NoteInfo describeHz (double hz, int keyRoot, int scale)
{
    NoteInfo n;
    n.hz = hz;
    const double m = hzToMidi (hz);
    n.midi = (int) std::lround (m);
    n.cents = (m - (double) n.midi) * 100.0;
    n.pitchClass = ((n.midi % 12) + 12) % 12;
    n.inKey = inScale (n.pitchClass, keyRoot, scale);
    n.degreeSemis = ((n.pitchClass - keyRoot) % 12 + 12) % 12;
    n.name = noteName (n.midi);
    return n;
}

KeyReport analyzeKey (const Settings& s, const PatternData& pattern, double samplePitchHz)
{
    KeyReport r;
    const int key = ((s.global.keyRoot % 12) + 12) % 12;
    const int scale = std::min (kNumScales - 1, std::max (0, s.global.keyScale));
    const bool chromatic = scale == kNumScales - 1;
    const double tol = std::max (1.0, (double) s.global.keyTolCents);
    const std::string keyName = std::string (kKeyNames[key]) + " " + kScaleNames[scale];

    auto worst = [&r] (Status st)
    {
        if (st == Status::Bad || (st == Status::Warn && r.overall == Status::Ok)) r.overall = st;
    };

    //--- Kick-Grundton ------------------------------------------------------------
    r.kick = describeHz (s.kick.endHz, key, scale);
    {
        KeyCheck c;
        c.title = "Kick-Grundton " + fmt ("%.1f Hz", r.kick.hz) + " = " + r.kick.name + fmt (" %+.0f ct", r.kick.cents);
        if (std::abs (r.kick.cents) > tol)
        {
            c.status = Status::Bad;
            c.detail = fmt ("Verstimmt um %.0f Cent (Toleranz %.0f). Kick schwebt gegen den Bass.", std::abs (r.kick.cents), tol);
        }
        else if (! chromatic && ! r.kick.inKey)
        {
            c.status = Status::Bad;
            c.detail = "Nicht in " + keyName + ".";
        }
        else if (chromatic || r.kick.degreeSemis == 0 || r.kick.degreeSemis == 7)
        {
            c.status = Status::Ok;
            c.detail = chromatic ? "Sauber gestimmt." : (r.kick.degreeSemis == 0 ? "Grundton der Tonart." : "Quinte der Tonart.");
        }
        else
        {
            c.status = Status::Warn;
            c.detail = std::string ("In der Tonart, aber ") + intervalName (r.kick.degreeSemis) + " - Grundton oder Quinte klingen meist am stabilsten.";
        }
        worst (c.status);
        r.checks.push_back (c);
    }

    //--- Bass-Grundton --------------------------------------------------------------
    r.bassRoot = describeHz (midiToHz (s.global.rootNote), key, scale);
    {
        KeyCheck c;
        c.title = "Bass-Grundton " + r.bassRoot.name + fmt (" (%.1f Hz)", r.bassRoot.hz);
        if (! chromatic && ! r.bassRoot.inKey)      { c.status = Status::Bad;  c.detail = "Nicht in " + keyName + "."; }
        else if (chromatic || r.bassRoot.degreeSemis == 0) { c.status = Status::Ok; c.detail = chromatic ? "Keine Skala gewählt." : "Grundton der Tonart."; }
        else { c.status = Status::Warn; c.detail = std::string ("In der Tonart (") + intervalName (r.bassRoot.degreeSemis) + "), aber nicht der Grundton."; }
        worst (c.status);
        r.checks.push_back (c);
    }

    //--- Pattern-Töne ---------------------------------------------------------------
    {
        KeyCheck c;
        std::string bad;
        int numBad = 0, numBass = 0;
        const int ns = stepsForGrid (s.global.seqGrid);
        for (int i = 0; i < ns; ++i)
        {
            const Step st = pattern.get (i);
            if (! st.bass) continue;
            ++numBass;
            const int midi = std::min (127, std::max (0, s.global.rootNote + st.note));
            const double hz = midiToHz (midi);
            if (std::find_if (r.bassNoteHz.begin(), r.bassNoteHz.end(), [hz] (double v) { return std::abs (v - hz) < 0.01; }) == r.bassNoteHz.end())
                r.bassNoteHz.push_back (hz);
            if (! chromatic && ! inScale (midi % 12, key, scale))
            {
                r.stepOutOfKey[(size_t) i] = true;
                ++numBad;
                bad += (bad.empty() ? "" : ", ") + std::to_string (i + 1) + " (" + noteName (midi) + ")";
            }
        }
        c.title = fmt ("Pattern-Töne: %.0f Bassnoten", numBass);
        if (numBad > 0) { c.status = Status::Bad; c.detail = "Außerhalb der Skala bei Schritt " + bad + "."; }
        else { c.status = numBass > 0 ? Status::Ok : Status::Off; c.detail = numBass > 0 ? "Alle Töne in der Skala." : "Keine Bassnoten im Pattern."; }
        worst (c.status);
        r.checks.push_back (c);
    }

    //--- Kick gegen Bass: Intervall und Schwebung --------------------------------------
    {
        KeyCheck c;
        const double fk = r.kick.hz, fb = r.bassRoot.hz;
        double cents = 1200.0 * std::log2 (fk / fb);
        cents = std::fmod (std::fmod (cents, 1200.0) + 1200.0, 1200.0);
        r.intervalCents = cents;
        const double octaves = std::round (std::log2 (fk / fb));
        r.beatHz = std::abs (fk - fb * std::pow (2.0, octaves));
        const int semis = (int) std::lround (cents / 100.0) % 12;
        const double dev = std::abs (cents - 100.0 * std::lround (cents / 100.0));
        c.title = std::string ("Kick zu Bass: ") + intervalName (semis) + fmt (" (%.0f ct), Schwebung %.2f Hz", cents, r.beatHz);
        const double degPerNote = r.beatHz * 0.1 * 360.0;   // Phasendrift über eine 100-ms-Note
        if ((semis == 0) && dev <= tol)
        {
            c.status = Status::Ok;
            c.detail = r.beatHz < 0.05 ? "Gleicher Ton - Phase bleibt über die Note stabil (ideal für Auto Fit)."
                                       : fmt ("Fast gleich - Phase dreht sich um ca. %.0f Grad pro 100 ms.", degPerNote);
        }
        else if ((semis == 7 || semis == 5) && dev <= tol)
        {
            c.status = Status::Warn;
            c.detail = "Konsonant, aber nicht derselbe Ton: die Phasenlage wechselt ständig, Auslöschung lässt sich nur teilweise vermeiden.";
        }
        else
        {
            c.status = Status::Bad;
            c.detail = fmt ("Passt nicht zusammen: Phase dreht sich um ca. %.0f Grad pro 100 ms -> abwechselnd Verstärkung und Auslöschung.", degPerNote);
        }
        worst (c.status);
        r.checks.push_back (c);
    }

    //--- Sample-Grundton ----------------------------------------------------------------
    if (s.kick.smpOn && samplePitchHz > 0.0)
    {
        r.sampleValid = true;
        const double hz = samplePitchHz * std::pow (2.0, (double) s.kick.smpTuneSt / 12.0);
        r.sample = describeHz (hz, key, scale);
        KeyCheck c;
        c.title = "Kick-Sample " + fmt ("%.1f Hz", hz) + " = " + r.sample.name + fmt (" %+.0f ct", r.sample.cents);
        double cents = 1200.0 * std::log2 (hz / r.kick.hz);
        cents = std::fmod (std::fmod (cents, 1200.0) + 1200.0, 1200.0);
        const double offOct = std::min (cents, 1200.0 - cents);
        if (offOct <= tol) { c.status = Status::Ok; c.detail = "Passt zum Kick-Grundton (Oktave gleich)."; }
        else if (! chromatic && r.sample.inKey && std::abs (r.sample.cents) <= tol)
        {
            c.status = Status::Warn;
            c.detail = fmt ("In der Tonart, aber %.0f Cent vom Kick-Grundton entfernt - Schichten können schweben.", offOct);
        }
        else { c.status = Status::Bad; c.detail = fmt ("Weicht %.0f Cent vom Kick-Grundton ab.", offOct); }
        worst (c.status);
        r.checks.push_back (c);
    }

    return r;
}

double kickHzForKey (int keyRoot, double currentHz)
{
    double best = 0.0, bestDist = 1.0e9;
    for (int midi = 12; midi <= 60; ++midi)
        if (((midi % 12) + 12) % 12 == ((keyRoot % 12) + 12) % 12)
        {
            const double hz = midiToHz (midi);
            if (hz < 30.0 || hz > 70.0) continue;
            const double d = std::abs (std::log2 (hz / std::max (1.0, currentHz)));
            if (d < bestDist) { bestDist = d; best = hz; }
        }
    return best > 0.0 ? best : currentHz;
}

int bassRootForKey (int keyRoot, int currentRoot)
{
    int best = currentRoot, bestDist = 1000;
    for (int midi = 12; midi <= 60; ++midi)
        if (midi % 12 == ((keyRoot % 12) + 12) % 12 && std::abs (midi - currentRoot) < bestDist)
        {
            bestDist = std::abs (midi - currentRoot);
            best = midi;
        }
    return best;
}

double sampleTuneForKick (double samplePitchHz, double kickHz, double currentTune)
{
    if (samplePitchHz <= 0.0 || kickHz <= 0.0) return currentTune;
    double semis = 12.0 * std::log2 (kickHz / samplePitchHz);
    // Oktave so wählen, dass die Änderung gegenüber der aktuellen Stimmung am kleinsten ist
    while (semis - currentTune > 6.0) semis -= 12.0;
    while (semis - currentTune < -6.0) semis += 12.0;
    return std::min (24.0, std::max (-24.0, semis));
}

PatternData snapPatternToScale (const PatternData& p, int bassRoot, int keyRoot, int scale)
{
    PatternData out = p;
    for (int i = 0; i < kNumSteps; ++i)
    {
        Step st = p.get (i);
        if (! st.bass) continue;
        const int midi = bassRoot + st.note;
        if (inScale (((midi % 12) + 12) % 12, keyRoot, scale)) continue;
        for (int d : { -1, 1, -2, 2 })
            if (inScale ((((midi + d) % 12) + 12) % 12, keyRoot, scale) && st.note + d >= -24 && st.note + d <= 24)
            {
                st.note += d;
                break;
            }
        out.set (i, st);
    }
    return out;
}

} // namespace samsi
