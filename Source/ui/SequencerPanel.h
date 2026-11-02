#pragma once

#include "SamsiLookAndFeel.h"
#include "../dsp/Settings.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

/** 16/24/32-Schritt-Raster mit getrennten Kick- und Bass-Lanes.
    Zeilen: KICK, BASS, TON (Halbtöne), LÄNGE (%), VEL, SLIDE. */
class StepGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (SamsiBasslineProcessor& p);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    void setPlayStep (int s) { if (s != playStep) { playStep = s; repaint(); } }
    void refresh() { repaint(); }

    enum Row { RowKick, RowBass, RowNote, RowLen, RowVel, RowSlide, NumRows };

private:
    juce::Rectangle<float> cell (int row, int step) const;
    bool hit (juce::Point<float> p, int& row, int& step) const;
    float labelWidth() const { return 54.0f; }

    SamsiBasslineProcessor& proc;
    int playStep = -1;
    int numSteps = 16;
    int dragRow = -1, dragStep = -1;
    float dragStartY = 0.0f;
    int dragStartValue = 0;
    bool paintValue = false;        // beim Ziehen über Ein/Aus-Zeilen
};

class SequencerPanel : public PanelFrame
{
public:
    SequencerPanel (SamsiBasslineProcessor& p, juce::AudioProcessorValueTreeState& apvts);
    void resized() override;
    void setPlayStep (int s) { grid.setPlayStep (s); }
    void refresh() { grid.refresh(); }

private:
    SamsiBasslineProcessor& proc;
    StepGrid grid;
    Toggle seqOn;
    Knob swing, root, midiKick;
    Choice mode, lookahead, gridChoice;
    juce::TextButton presetKBBB { "K-B-B-B" }, presetOff { "OFFBEAT" }, presetClear { "LEER" };
};

} // namespace samsi::ui
