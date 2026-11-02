#pragma once

#include "PluginProcessor.h"
#include "ui/BassPanel.h"
#include "ui/FitPanel.h"
#include "ui/FlowerOrb.h"
#include "ui/AnalyzerView.h"
#include "analysis/LiveAnalyzer.h"
#include "ui/KickPanel.h"
#include "ui/MixPanel.h"
#include "ui/SamsiLookAndFeel.h"
#include "ui/SequencerPanel.h"

/** SAMSARA-Emblem: das Logo im Kreis, umgeben von einem langsam drehenden Blütenkranz,
    der mit jedem Kick aufleuchtet. */
class SamsaraEmblem : public juce::Component
{
public:
    SamsaraEmblem();
    void paint (juce::Graphics&) override;
    void advance (float kickLevel);

private:
    juce::Image logo;
    float rotation = 0.0f, pulse = 0.0f;
};

/** Oberfläche: alles wird in einer festen Basisgröße (1560 x 980) angeordnet und als
    Ganzes skaliert -> verlustfrei skalierbar, gleiche Proportionen in jeder Größe.
    Radial aufgebaut: in der Mitte das Auto-Fit-Orb (Blume des Lebens), darüber die Phasenansicht,
    darunter Messung/Key; links Kick + Sequencer, rechts Bass + Auto-Fit-Details. */
class SamsiBasslineEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int kBaseW = 1560, kBaseH = 980;

    explicit SamsiBasslineEditor (SamsiBasslineProcessor&);
    ~SamsiBasslineEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Für Screenshots/Tests: Seiten der Bereiche wählen. */
    void showPages (int kickPage, int bassPage, int mixPage, int fitPage = 0);
    /** 0 = Synth, 1 = Analyzer/Utility */
    void showView (int v);
    /** Für Tests/Screenshots: Live-Daten direkt einspeisen */
    samsi::LiveAnalyzer& getLiveAnalyzer() noexcept { return live; }
    /** Für Tests: Live-Wellenform (0 Kick, 1 Bass) der Synth- (view 0) oder Analyzer-Ansicht (view 1). */
    samsi::ui::LiveWaveView* getLiveView (int which, int view);
    /** Für Tests: einen Timer-Durchlauf ausführen. */
    void tickForTest() { timerCallback(); }

private:
    void timerCallback() override;

    samsi::LiveAnalyzer live;   // vor Content: Anzeigen halten Zeiger darauf

    class Content : public juce::Component
    {
    public:
        Content (SamsiBasslineProcessor& p, samsi::LiveAnalyzer& live);
        void setView (int v);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refreshPresetBox();

        SamsiBasslineProcessor& proc;
        SamsaraEmblem emblem;
        samsi::ui::KickPanel kick;
        samsi::ui::BassPanel bass;
        samsi::ui::MixPanel mix, measure;
        samsi::ui::FlowerOrb orb;
        samsi::ui::ViewOptions viewOpts;
        samsi::ui::TabStrip viewTabs;
        samsi::ui::AnalyzerView analyzer;
        int view = 0;
        samsi::ui::SequencerPanel seq;
        samsi::ui::FitPanel fit;
        samsi::ui::Knob master;
        samsi::ui::Toggle busClip;
        samsi::ui::Knob busDrive;
        juce::ComboBox presetBox;
        juce::TextButton prevButton { "<" }, nextButton { ">" }, saveButton { "SPEICHERN" }, loadButton { "LADEN" };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String presetHint;
        float meter[3] {};

    private:
        juce::Image background;
        juce::Rectangle<int> meterArea, busArea, presetArea, titleArea, viewArea;
    };

    SamsiBasslineProcessor& proc;
    samsi::ui::SamsiLookAndFeel lnf;   // muss vor allen Kindkomponenten existieren
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltips { this, 600 };

    void updateOrb();
    uint64_t requestedHash = 0;
    bool keyOk = true;
    int ticks = 0, shownProgram = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamsiBasslineEditor)
};
