#pragma once

#include "SamsiLookAndFeel.h"
#include "AnalyzerView.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

/** Kick→Bass-Glue (Seite GLUE in AUTO FIT · DETAILS): Sidechain-Kompressor und zwei dynamische
    EQ-Bänder auf dem Bass, ausgelöst von der Kick. Kurve zeigt die statische Lage (0 dB) und die
    momentane Absenkung, Balken die Kompressor-Reduktion. */
class GluePanel : public juce::Component
{
public:
    GluePanel (SamsiBasslineProcessor& p);
    void resized() override;
    void paint (juce::Graphics&) override;
    /** vom Editor-Timer */
    void refresh();
    void showSub (int s) { sub.setSelected (s, true); }

private:
    void setSub (int s);
    juce::Rectangle<int> curveArea;
    SamsiBasslineProcessor& proc;
    Toggle on, compOn, b1On, b2On;
    Pills sub;
    juce::TextButton autoButton { "AUTO" };
    Choice style, b1Type, b2Type;
    Knob thr, ratio, att, rel, knee, range, scHp, scLp, mix;
    Knob b1Freq, b1Q, b1Range, b1Thr, b1Att, b1Rel;
    Knob b2Freq, b2Q, b2Range, b2Thr, b2Att, b2Rel;
    float grShown = 0.0f, grHold = 0.0f;
    juce::String message;
};

} // namespace samsi::ui
