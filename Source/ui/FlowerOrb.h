#pragma once

#include "SamsiLookAndFeel.h"

namespace samsi::ui
{

/** Zentrales "Blume des Lebens"-Orb – gleichzeitig der Auto-Fit-Knopf.

    - Das Muster wird prozedural gezeichnet (Kreise im Sechseckgitter, deren Linsen als
      Blütenblätter gefüllt werden, auf einer Mint/Türkis-Kugel) und als Bild zwischengespeichert.
    - Klick startet Auto Fit; während der Analyse läuft ein Fortschrittsring.
    - Wenn die gemessene Phase passt (siehe State::aligned), pulsiert das Orb im Takt der Kick.
      Sonst atmet es nur schwach, und ein Hinweis zeigt die Abweichung.
    Das Orb zeigt nur Messwerte an, es bewertet nichts selbst. */
class FlowerOrb : public juce::Component, public juce::SettableTooltipClient
{
public:
    struct State
    {
        bool running = false;     // Auto Fit läuft
        float progress = 0.0f;    // 0..1
        bool aligned = false;     // Messung im Ziel: Puls an
        bool known = false;       // gibt es überhaupt eine aktuelle Messung?
        float worstCancelDb = 0.0f;
        bool keyOk = true;
        bool fitOn = true;
        juce::String line1, line2;  // Statuszeilen unter dem Titel
    };

    FlowerOrb();

    std::function<void()> onClick;

    void setState (const State& s);
    /** pro Editor-Tick (30 Hz): kickLevel = aktueller Kick-Pegel 0..1 */
    void advance (float kickLevel);

    void paint (juce::Graphics&) override;
    void resized() override { cache = {}; }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override  { hover = false; pressed = false; repaint(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool hitTest (int x, int y) override;

    /** Kugel-Radius und -Mittelpunkt (lokale Koordinaten) */
    juce::Point<float> orbCentre() const;
    float orbRadius() const;

    /** Zeichnet die Blume des Lebens (für Tests/Hintergrund auch einzeln nutzbar). */
    static void drawFlower (juce::Graphics& g, juce::Point<float> c, float R);

private:
    State state;
    juce::Image cache;
    float pulse = 0.0f, breath = 0.0f, spin = 0.0f, glow = 0.0f;
    bool hover = false, pressed = false;
};

} // namespace samsi::ui
