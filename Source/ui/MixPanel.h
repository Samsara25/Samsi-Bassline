#pragma once

#include "SamsiLookAndFeel.h"
#include "../fit/AnalysisService.h"
#include "KeyView.h"
#include "AnalyzerView.h"

namespace samsi::ui
{

/** Gemeinsame, zoombare Ansicht von Kick, Bass und Summe (real gerenderter Takt),
    mit markierten Überlappungsfenstern und Phasen-/Kohärenzstreifen. */
class ScopeView : public juce::Component
{
public:
    enum class Mode { Wave, Spectrum };

    void setData (std::shared_ptr<const PreviewData> d);
    void setMode (Mode m) { mode = m; repaint(); }
    void setStale (bool s) { if (s != stale) { stale = s; repaint(); } }

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverX = -1.0f; repaint(); }

    bool showKick = true, showBass = true, showSum = true;

private:
    void paintWave (juce::Graphics&, juce::Rectangle<float> area);
    void paintSpectrum (juce::Graphics&, juce::Rectangle<float> area);
    void drawTrace (juce::Graphics&, juce::Rectangle<float> r, const std::vector<float>& x, juce::Colour c, float scale, float thickness);
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> phaseArea() const;
    juce::Rectangle<float> legendRect (int i) const;
    void clampView();

    std::shared_ptr<const PreviewData> data;
    Mode mode = Mode::Wave;
    double viewStart = 0.0, viewLen = 0.0;
    double dragStartView = 0.0;
    float dragStartX = 0.0f, hoverX = -1.0f;
    bool stale = false;
};

/** Pegelbalken je Band für Kick/Bass/Summe. */
class BandMeter : public juce::Component
{
public:
    void setData (std::shared_ptr<const PreviewData> d) { data = std::move (d); repaint(); }
    void paint (juce::Graphics&) override;

private:
    std::shared_ptr<const PreviewData> data;
};

/** Zwei Rollen, damit das Auto-Fit-Orb in der Mitte sitzen kann:
    Scope   – oben in der Mitte: zoombare Wellenform/Spektrum von Kick, Bass und Summe, Monitor-Wahl.
    Measure – unten in der Mitte: Seite MESSUNG (Bandpegel, Phasen-Kennwerte) und Seite KEY (Tonart/Frequenz). */
class MixPanel : public PanelFrame
{
public:
    enum class Role { Scope, Measure };
    MixPanel (SamsiBasslineProcessor& p, Role role);
    void updateKey() { if (role == Role::Measure && tabs.getSelected() == 1) keyView.update(); }
    void showPage (int page) { tabs.setSelected (page); }
    void resized() override;
    void paint (juce::Graphics&) override;

    void setPreview (std::shared_ptr<const PreviewData> d, bool stale);

    /** Live-Wellenformen (KICK oben, BASS unten) in der Scope-Rolle anbinden. */
    void attachLive (const LiveAnalyzer* an, ViewOptions& opts, SamsiBasslineProcessor* p = nullptr);
    LiveWaveView* getLiveView (int i) { return i == 0 ? liveKick.get() : liveBass.get(); }
    void refreshLive();

private:
    void setPage (int page);
    Role role;
    TabStrip tabs;
    KeyView keyView;
    ScopeView scope;
    BandMeter bands;
    juce::TextButton waveButton { "WELLE" }, specButton { "SPEKTRUM" }, liveButton { "LIVE" };
    std::unique_ptr<LiveWaveView> liveKick, liveBass;
    void setScopeMode (int m);   // 0 Live, 1 Welle, 2 Spektrum
    int scopeMode = 1;
    ViewOptions* optsPtr = nullptr;
    Choice monitor;
    juce::Rectangle<int> infoArea;
    std::shared_ptr<const PreviewData> data;
};

} // namespace samsi::ui
