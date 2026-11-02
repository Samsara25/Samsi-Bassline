#pragma once

#include "SamsiLookAndFeel.h"
#include "../dsp/Layers.h"
#include "KickPanel.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

/** Zeigt die gewählte Wavetable: mehrere Frames gestaffelt (3D), der aktuelle Frame leuchtet. */
class WavePreview : public juce::Component
{
public:
    void setTable (std::shared_ptr<const WavetableData> t, float pos, bool userSlotEmpty);
    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<const WavetableData> table;
    float pos = 0.0f;
    bool emptyUser = false;
};

class BassPanel : public PanelFrame, public juce::FileDragAndDropTarget
{
public:
    explicit BassPanel (SamsiBasslineProcessor& p);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    void update();
    void showPage (int page) { tabs.setSelected (page); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void setPage (int page);
    void load (const juce::File& f);

    SamsiBasslineProcessor& proc;
    TabStrip tabs;
    void loadSample (const juce::File& f);
    std::vector<std::unique_ptr<juce::Component>> pages[4];   // Zeile 1: Oszillator / Wavetable / Pick / Sample
    std::vector<std::unique_ptr<juce::Component>> rows[4];    // Filter, F-Hüllkurve, Amp, Stereo
    WavePreview preview;
    juce::TextButton loadButton { "LADEN" }, clearButton { "X" };
    juce::TextButton smpLoadButton { "LADEN" }, smpClearButton { "X" };
    SampleStrip smpStrip;
    std::unique_ptr<Toggle> smpThru;
    std::shared_ptr<const KickSample> shownSample;
    juce::String smpMessage;
    bool smpError = false;
    juce::Rectangle<int> smpInfo;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    juce::Rectangle<int> labels[4], pickHelp;
    bool dragOver = false;
};

} // namespace samsi::ui
