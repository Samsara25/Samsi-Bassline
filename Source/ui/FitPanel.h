#pragma once

#include "SamsiLookAndFeel.h"
#include "../fit/AutoFit.h"
#include "GluePanel.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

/** Auto-Fit-Details (gestartet wird über das Orb in der Mitte):
    Seite STEUERUNG – Sperren, A/B, Undo, manuelle Übersteuerung der Ergebnisse;
    Seite MESSWERTE – reale Messwerte vorher/nachher und Hinweise. */
class FitPanel : public PanelFrame
{
public:
    FitPanel (SamsiBasslineProcessor& p, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
    void paint (juce::Graphics&) override;

    /** vom Editor-Timer aufgerufen */
    void update();
    void showPage (int p) { tabs.setSelected (p); }
    void refreshGlue() { if (tabs.getSelected() == 2) glue.refresh(); }

private:
    void setPage (int page);
    SamsiBasslineProcessor& proc;
    TabStrip tabs;
    GluePanel glue;
    juce::TextButton undoButton { "UNDO" }, abButton { "NACHHER (Fit aktiv)" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> abAttachment;
    Toggle loudMatch, lockKick, lockTiming, lockBassTone, polarity, duckLow;
    Knob maxShift, phase, offset, duckDb, duckMs, duckHz, trim, attAdd, kickTail;
    Choice phaseMode;

    juce::Rectangle<int> pageArea, statusArea, tableArea, notesArea, labelLocks, labelResult;
    juce::String status;
    juce::Colour statusColour = col::textDim;
    float progress = 0.0f;
    bool running = false;
    std::shared_ptr<const FitResult> shownFit;
};

} // namespace samsi::ui
