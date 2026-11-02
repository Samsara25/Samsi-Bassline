#pragma once

#include "../dsp/Settings.h"

#include <array>
#include <string>
#include <vector>

namespace samsi
{

constexpr int kNumScales = 7;
inline constexpr const char* kScaleNames[kNumScales] = {
    "Moll (Äolisch)", "Phrygisch", "Harmonisch Moll", "Phrygisch-Dominant", "Dur", "Dorisch", "Chromatisch (aus)" };
inline constexpr const char* kKeyNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "H" };

/** true, wenn die Tonhöhenklasse (0..11) in der Skala liegt. */
bool inScale (int pitchClass, int keyRoot, int scale) noexcept;

double midiToHz (double midi) noexcept;
double hzToMidi (double hz) noexcept;

struct NoteInfo
{
    double hz = 0.0;
    int midi = 0;            // nächstgelegene Note
    double cents = 0.0;      // Abweichung von dieser Note
    int pitchClass = 0;
    bool inKey = true;
    int degreeSemis = 0;     // Abstand zum Tonart-Grundton in Halbtönen (0..11)
    std::string name;        // z. B. "F#0" (Ableton-Notation, C3 = MIDI 60)
};

NoteInfo describeHz (double hz, int keyRoot, int scale);

enum class Status { Ok, Warn, Bad, Off };

struct KeyCheck
{
    Status status = Status::Off;
    std::string title, detail;
};

struct KeyReport
{
    NoteInfo kick, bassRoot, sample;
    bool sampleValid = false;
    std::vector<double> bassNoteHz;               // Frequenzen der Pattern-Töne (ohne Dubletten)
    std::array<bool, kNumSteps> stepOutOfKey {};  // Bass-Schritte außerhalb der Skala
    double intervalCents = 0.0;                   // Kick-Grundton zu Bass-Grundton (oktavgefaltet, 0..1200)
    double beatHz = 0.0;                          // Schwebung zwischen Kick-Grundton und nächster Bass-Oktave
    std::vector<KeyCheck> checks;
    Status overall = Status::Ok;
};

/** samplePitchHz: erkannter Sample-Grundton (0 = keiner); die Sample-Stimmung wird berücksichtigt. */
KeyReport analyzeKey (const Settings& s, const PatternData& pattern, double samplePitchHz);

//--- Korrekturen (liefern neue Werte, ändern nichts selbst) -------------------------
/** Kick-Grundton auf den Tonart-Grundton (Oktave: nächster Wert im Bereich 30..70 Hz). */
double kickHzForKey (int keyRoot, double currentHz);
/** Bass-Grundton (MIDI) auf den Tonart-Grundton, nächstgelegene Oktave im Bereich 12..60. */
int bassRootForKey (int keyRoot, int currentRoot);
/** Sample-Stimmung (Halbtöne), damit der Sample-Grundton zum Kick-Grundton passt (oktavgefaltet). */
double sampleTuneForKick (double samplePitchHz, double kickHz, double currentTune);
/** Verschiebt Pattern-Töne außerhalb der Skala um einen Halbton (abwärts bevorzugt). */
PatternData snapPatternToScale (const PatternData& p, int bassRoot, int keyRoot, int scale);

} // namespace samsi
