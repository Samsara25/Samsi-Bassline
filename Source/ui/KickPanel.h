#pragma once

#include "SamsiLookAndFeel.h"
#include "../dsp/Layers.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

/** Grafischer Editor für Kick-Pitch- und Amplitudenhüllkurve.
    Ziehpunkte steuern direkt die Host-Parameter (mit Gesten -> automatisierbar). */
class KickEnvelopeEditor : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit KickEnvelopeEditor (juce::AudioProcessorValueTreeState& apvts);
    ~KickEnvelopeEditor() override;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    enum Handle { None = -1, HoldEnd, DecayEnd, TailEnd, DecayCurve, PitchStart, PitchEnd, PitchCurve, NumHandles };

    struct Values { float start, end, pitchMs, pitchCurve, attack, hold, decay, decayCurve, tailLevel, tailMs; };
    Values read() const;
    void timerCallback() override;

    juce::Rectangle<float> plotArea() const;
    float timeToX (float ms) const;
    float xToTime (float x) const;
    float ampToY (float a) const;
    float yToAmp (float y) const;
    float hzToY (float hz) const;
    float yToHz (float y) const;
    juce::Point<float> handlePos (Handle h, const Values& v) const;
    Handle findHandle (juce::Point<float> p) const;

    juce::RangedAudioParameter* param (const char* id) const;
    void setParam (const char* id, float plain);
    void beginGestures (Handle h);
    void endGestures();

    juce::AudioProcessorValueTreeState& apvts;
    Values cached {};
    float viewMs = 400.0f;          // eingefroren während des Ziehens
    Handle hover = None, dragging = None;
    juce::Point<float> dragStart;
    float dragStartCurve = 1.0f;
    juce::StringArray activeGestures;
};

/** Wellenform des geladenen Kick-Samples mit Start/Länge/Fade-Markierung. */
class SampleStrip : public juce::Component
{
public:
    void setSample (std::shared_ptr<const KickSample> s);
    void setMarkers (float startMs, float lengthMs, float fadeMs, float tuneSt);
    void paint (juce::Graphics&) override;
    /** Kompakt: ohne Textzeile (für das Sample-Fenster neben der Hüllkurve) */
    void setCompact (bool c) { compact = c; repaint(); }

private:
    bool compact = false;
    std::shared_ptr<const KickSample> smp;
    std::vector<float> peaks;
    float startMs = 0, lengthMs = 400, fadeMs = 60, tuneSt = 0;
};

class KickPanel : public PanelFrame, public juce::FileDragAndDropTarget
{
public:
    explicit KickPanel (SamsiBasslineProcessor& p);
    void resized() override;
    void paintOverChildren (juce::Graphics&) override;
    /** vom Editor-Timer aufgerufen */
    void update();
    void showPage (int page) { tabs.setSelected (page); }

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void load (const juce::File& f);
    void setPage (int page);

    SamsiBasslineProcessor& proc;
    KickEnvelopeEditor envelope;
    std::vector<std::unique_ptr<Knob>> knobs;     // 16 Synth-Regler
    TabStrip tabs;
    // Sample-Seite
    juce::TextButton loadButton { "LADEN" }, clearButton { "X" }, alignButton { "AUSRICHTEN" };
    Toggle smpOn, smpInvert;
    SampleStrip strip;
    std::vector<std::unique_ptr<Knob>> smpKnobs;
    // Sample-Fenster rechts neben der Hüllkurve (immer sichtbar, wie auf der Bass-Seite)
    SampleStrip sideStrip;
    juce::TextButton sideLoad { "LADEN" }, sideClear { "X" }, sideAlign { "AUSRICHTEN" };
    Toggle sideOn;
    juce::Rectangle<int> sideBox, sideInfo;
    void openChooser();
    // Seite SAMPLE-MOD: Kick-Regler modulieren das Sample
    Knob modAmp, modPitch;
    Toggle modKey, modDrive;
    juce::Rectangle<int> labelMod, modHelp;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    bool messageIsError = false;
    std::shared_ptr<const KickSample> shown;
    bool dragOver = false;

    juce::Rectangle<int> labelPitch, labelAmp, labelClick, labelSample;
};

} // namespace samsi::ui
