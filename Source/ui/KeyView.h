#pragma once

#include "SamsiLookAndFeel.h"
#include "../content/KeyAnalysis.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

/** Tonart- und Frequenz-Abgleich: zeigt Kick-Grundton, Bass-Grundton, Pattern-Töne und
    Sample-Grundton auf einer Frequenzleiter (20–250 Hz) und markiert Unpassendes rot. */
class KeyView : public juce::Component
{
public:
    explicit KeyView (SamsiBasslineProcessor& p);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::Colour statusColour (Status s);

private:
    float hzToX (double hz) const;

    SamsiBasslineProcessor& proc;
    Choice keyRoot, keyScale;
    Knob tolerance;
    juce::TextButton fixKick { "KICK > TONART" }, fixBass { "BASS > TONART" },
                     fixSample { "SAMPLE > KICK" }, fixPattern { "TÖNE > SKALA" };
    KeyReport report;
    juce::Rectangle<int> ladder, checks;
};

} // namespace samsi::ui
