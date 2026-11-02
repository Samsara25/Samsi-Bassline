#pragma once

#include "SamsiLookAndFeel.h"
#include "../analysis/LiveAnalyzer.h"

class SamsiBasslineProcessor;

namespace samsi::ui
{

//==============================================================================
/** Kleine Auswahl-Leiste (Pillen) für Anzeige-Optionen, die keine Klangparameter sind. */
class Pills : public juce::Component, public juce::SettableTooltipClient
{
public:
    Pills (juce::StringArray items, juce::Colour accent, bool multi = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void setSelected (int i, bool notify = false);
    int getSelected() const noexcept { return selected; }
    /** Mehrfachauswahl: Bitmaske */
    void setMask (int m, bool notify = false);
    int getMask() const noexcept { return mask; }
    std::function<void (int)> onChange;
    int idealWidth() const;

private:
    juce::StringArray items;
    juce::Colour accent;
    bool multi = false;
    int selected = 0, mask = 1;
};

/** Knopf, der beim Klick durch Werte schaltet ("FFT 8192" -> "FFT 16384" ...). Rechtsklick rückwärts. */
class CycleButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    CycleButton (juce::String prefix, juce::StringArray values, int initial, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int getIndex() const noexcept { return index; }
    void setIndex (int i) { index = juce::jlimit (0, values.size() - 1, i); repaint(); }
    std::function<void (int)> onChange;
    int idealWidth() const;

private:
    juce::String prefix;
    juce::StringArray values;
    int index = 0;
    juce::Colour accent;
};

//==============================================================================
/** Anzeige-Optionen pro Plugin-Instanz (im Projekt gespeichert, keine Automation). */
struct ViewOptions
{
    explicit ViewOptions (juce::ValueTree s) : state (std::move (s)) {}
    int get (const char* key, int def) const { return (int) state.getProperty (key, def); }
    void set (const char* key, int v) { state.setProperty (key, v, nullptr); }
    juce::ValueTree state;
};

//==============================================================================
/** Live-Wellenform eines Elements (KICK oder BASS): jeder neue Kick bzw. jede neue Bassnote
    wird ab ihrem Start mitgeschnitten. Der neueste Mitschnitt leuchtet, die vorherigen
    verblassen (Nachleuchten) – so sieht man, wie sich die Form beim Drehen ändert. */
class LiveWaveView : public juce::Component, public juce::SettableTooltipClient, public juce::FileDragAndDropTarget
{
public:
    enum class Kind { Kick, Bass };
    LiveWaveView (Kind k, const LiveAnalyzer* an, ViewOptions& opts);
    /** Mit Prozessor verbunden: Samples per Drag & Drop / Menü laden, ausrichten, Auto Fit. */
    void setProcessor (SamsiBasslineProcessor* p) { proc = p; }
    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;
    /** Lädt eine Datei als Kick- bzw. Bass-Sample (wie Ablegen). Liefert eine Meldung. */
    juce::String loadFile (const juce::File& f);
    const juce::String& getMessage() const noexcept { return message; }

private:
    void showMenu();
    void drawSampleReference (juce::Graphics& g, juce::Rectangle<float> plot, int span, float scale);

    Kind kind;
    const LiveAnalyzer* an;
    ViewOptions& opts;
    SamsiBasslineProcessor* proc = nullptr;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String message;
    bool messageError = false;
    juce::uint32 messageTime = 0;
    bool dragOver = false;
    float zoomMs = 0.0f;
    bool autoGain = true, trails = true;
    int64_t lastSerial = -1;
    int lastLen = -1;
};

//==============================================================================
/** Oszilloskop: Signal-Trigger (Kick-Start bzw. steigende Flanke, 5..200 ms) oder Beat
    (ein Viertel bei Host-Tempo, zoombar bis ~5 ms). Spuren OUT / KICK / BASS / SIDECHAIN,
    Mono oder Stereo, Auto-Gain. */
class LiveScope : public juce::Component
{
public:
    LiveScope (const LiveAnalyzer* an, ViewOptions& opts);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh() { repaint(); }

private:
    juce::Rectangle<int> plotArea() const;
    const LiveAnalyzer* an;
    ViewOptions& opts;
    Pills trigger, layout, traces;
    CycleButton gain;
    float windowMs = 50.0f, beatZoom = 1.0f;
};

//==============================================================================
/** Spektrum (Linie / Oktavbänder) und Spektrogramm mit musikalischem Raster. */
class SpectrumView : public juce::Component
{
public:
    SpectrumView (LiveAnalyzer* an, ViewOptions& opts);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = {}; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { std::fill (hold.begin(), hold.end(), -200.0f); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refresh();

private:
    juce::Rectangle<int> plotArea() const;
    float xToHz (float x, juce::Rectangle<float> r) const;
    float hzToX (float hz, juce::Rectangle<float> r) const;
    float dbToY (float db, juce::Rectangle<float> r) const;
    void computeCurve (int source, std::vector<float>& curve);
    void pushSpectrogramColumn();

    LiveAnalyzer* an;
    ViewOptions& opts;
    Pills mode, source;
    CycleButton fftSize, window, smooth, tilt, range, orient, palette;
    juce::TextButton holdButton { "PEAK HOLD" }, gridButton { "RASTER" }, scButton { "SC" };
    std::vector<float> spec, curveMain, curveKick, curveBass, curveSc, hold, disp, dispKick, dispBass;
    juce::Image gram;
    juce::Point<float> hover;
    int rootNote = 0;   // 0 = C
};

//==============================================================================
/** Goniometer (Lissajous / Punkte / Polar) mit Korrelation, Breite und Balance. */
class GonioView : public juce::Component
{
public:
    GonioView (const LiveAnalyzer* an, ViewOptions& opts);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();

private:
    juce::Rectangle<int> scopeArea() const;
    const LiveAnalyzer* an;
    ViewOptions& opts;
    Pills mode;
    CycleButton decay;
    juce::Image phosphor;
    int64_t lastTotal = 0;
    int flash = 0;
};

//==============================================================================
/** Pegel L/R (RMS-Füllung + Peak-Strich, -56..+9 dB), Peak/True Peak, LUFS S/M/I, RMS/Crest. */
class MeterStrip : public juce::Component
{
public:
    MeterStrip (SamsiBasslineProcessor& p, LiveAnalyzer* an, ViewOptions& opts);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh() { repaint(); }

private:
    SamsiBasslineProcessor& proc;
    LiveAnalyzer* an;
    ViewOptions& opts;
    juce::Rectangle<int> peakBox, lufsBox, rmsBox;
};

//==============================================================================
/** Hör-Auswahl L / R / ST / M / S (Shift-Klick auf L oder R = Dual-Mono) an u_listen. */
class ListenSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    ListenSwitch (juce::AudioProcessorValueTreeState& apvts);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::ParameterAttachment attach;
    int value = 0;
};

/** Utility-Leiste am Fuß der Analyse-Ansicht: alle Klangparameter der Ausgangs-Utility. */
class UtilityBar : public PanelFrame
{
public:
    UtilityBar (SamsiBasslineProcessor& p, LiveAnalyzer* an);
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh();

private:
    SamsiBasslineProcessor& proc;
    LiveAnalyzer* an;
    Toggle on, post, invL, invR, link, swap, dc, monoOn, phaseOn, tsOn, limOn;
    ListenSwitch listen;
    Knob monoHz, imager, tilt, phase, ts, mix, outL, outR, ceil;
    juce::TextButton freeze { "FREEZE" }, resetMeters { "RESET" };
    juce::Rectangle<int> grArea;
    std::vector<juce::Rectangle<int>> groups;
    std::vector<juce::String> groupNames;
};

//==============================================================================
/** Gesamte Analyse-Ansicht (zweite Seite des Editors). */
class AnalyzerView : public juce::Component
{
public:
    AnalyzerView (SamsiBasslineProcessor& p, LiveAnalyzer& an);
    void resized() override;
    /** vom Editor-Timer (30 Hz) */
    void refresh();

    ViewOptions opts;
    PanelFrame scopeFrame, kickFrame, bassFrame, meterFrame, specFrame, gonioFrame;
    LiveScope scope;
    LiveWaveView kickLive, bassLive;
    MeterStrip meters;
    SpectrumView spectrum;
    GonioView gonio;
    UtilityBar utility;

private:
    SamsiBasslineProcessor& proc;
    LiveAnalyzer& an;
};

} // namespace samsi::ui
