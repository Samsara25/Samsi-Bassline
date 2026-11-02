#include "MixPanel.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace samsi::ui
{

//==============================================================================
void ScopeView::setData (std::shared_ptr<const PreviewData> d)
{
    const bool lengthChanged = data == nullptr || d == nullptr || d->render.length() != data->render.length();
    data = std::move (d);
    if (lengthChanged && data != nullptr)
    {
        viewStart = 0.0;
        viewLen = data->render.length();
    }
    clampView();
    repaint();
}

void ScopeView::clampView()
{
    if (data == nullptr) return;
    const double len = data->render.length();
    viewLen = juce::jlimit (std::min (len, data->render.sampleRate * 0.004), len, viewLen <= 0.0 ? len : viewLen);
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, len - viewLen), viewStart);
}

juce::Rectangle<float> ScopeView::waveArea() const
{
    return getLocalBounds().toFloat().reduced (4.0f).withTrimmedTop (16.0f).withTrimmedBottom (34.0f);
}

juce::Rectangle<float> ScopeView::phaseArea() const
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    return r.removeFromBottom (32.0f).withTrimmedBottom (14.0f);
}

juce::Rectangle<float> ScopeView::legendRect (int i) const
{
    return { 8.0f + (float) i * 80.0f, 2.0f, 76.0f, 14.0f };
}

void ScopeView::drawTrace (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& x, juce::Colour c,
                           float scale, float thickness)
{
    if (x.empty()) return;
    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f * scale;
    const int w = juce::jmax (1, (int) r.getWidth());
    const double spp = viewLen / w;   // Samples pro Pixel

    juce::Path p;
    if (spp > 2.0)
    {
        // Min/Max-Hüllkurve pro Pixel
        juce::Path top, bottom;
        for (int px = 0; px < w; ++px)
        {
            const int a = (int) (viewStart + px * spp), b = juce::jmin ((int) x.size(), (int) (viewStart + (px + 1) * spp) + 1);
            float mn = 0.0f, mx = 0.0f;
            for (int i = juce::jmax (0, a); i < b; ++i) { mn = juce::jmin (mn, x[(size_t) i]); mx = juce::jmax (mx, x[(size_t) i]); }
            const float xx = r.getX() + (float) px;
            if (px == 0) { top.startNewSubPath (xx, mid - mx * half); bottom.startNewSubPath (xx, mid - mn * half); }
            else { top.lineTo (xx, mid - mx * half); bottom.lineTo (xx, mid - mn * half); }
        }
        g.setColour (c.withAlpha (0.18f));
        juce::Path fill (top);
        for (int px = w - 1; px >= 0; --px)
        {
            const int a = (int) (viewStart + px * spp), b = juce::jmin ((int) x.size(), (int) (viewStart + (px + 1) * spp) + 1);
            float mn = 0.0f;
            for (int i = juce::jmax (0, a); i < b; ++i) mn = juce::jmin (mn, x[(size_t) i]);
            fill.lineTo (r.getX() + (float) px, mid - mn * half);
        }
        fill.closeSubPath();
        g.fillPath (fill);
        g.setColour (c);
        g.strokePath (top, juce::PathStrokeType (thickness * 0.75f));
        g.strokePath (bottom, juce::PathStrokeType (thickness * 0.75f));
    }
    else
    {
        const int a = juce::jmax (0, (int) viewStart), b = juce::jmin ((int) x.size(), (int) (viewStart + viewLen) + 2);
        for (int i = a; i < b; ++i)
        {
            const float xx = r.getX() + (float) ((i - viewStart) / spp);
            const float yy = mid - x[(size_t) i] * half;
            if (i == a) p.startNewSubPath (xx, yy); else p.lineTo (xx, yy);
        }
        g.setColour (c.withAlpha (0.3f));
        g.strokePath (p, juce::PathStrokeType (thickness + 3.0f));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness));
    }
}

void ScopeView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xd0030816));
    g.fillRoundedRectangle (bounds, 8.0f);

    if (data == nullptr || data->render.length() == 0)
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (14.0f));
        g.drawText ("Berechne Vorschau ...", getLocalBounds(), juce::Justification::centred);
        return;
    }

    if (mode == Mode::Wave) paintWave (g, waveArea());
    else                    paintSpectrum (g, bounds.reduced (6.0f).withTrimmedTop (16.0f));

    // Legende (anklickbar)
    const juce::String names[3] = { "Kick", "Bass", "Summe" };
    const juce::Colour cols[3] = { col::kick, col::bass.brighter (0.3f), col::sum };
    const bool vis[3] = { showKick, showBass, showSum };
    g.setFont (uiFont (12.0f, true));
    for (int i = 0; i < 3; ++i)
    {
        auto lr = legendRect (i);
        g.setColour (cols[i].withAlpha (vis[i] ? 1.0f : 0.3f));
        g.fillRoundedRectangle (lr.removeFromLeft (14.0f).reduced (2.0f, 4.0f), 2.0f);
        g.drawText (names[i], lr.translated (3.0f, 0.0f), juce::Justification::centredLeft);
    }

    if (stale)
    {
        g.setColour (col::amber);
        g.setFont (uiFont (12.0f));
        g.drawText ("aktualisiere ...", bounds.reduced (8.0f, 2.0f).removeFromTop (14.0f), juce::Justification::centredRight);
    }
}

void ScopeView::paintWave (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto& rr = data->render;
    const double sr = rr.sampleRate;
    const double spq = 60.0 / rr.bpm * sr;
    auto sx = [&] (double s) { return (float) (r.getX() + (s - viewStart) / viewLen * r.getWidth()); };

    // Überlappungsfenster
    for (auto& w : data->metrics.overlapWindows)
    {
        const float x0 = juce::jmax (r.getX(), sx (w.first)), x1 = juce::jmin (r.getRight(), sx (w.second));
        if (x1 <= x0) continue;
        g.setColour (col::amber.withAlpha (0.08f));
        g.fillRect (x0, r.getY(), x1 - x0, r.getHeight());
        g.setColour (col::amber.withAlpha (0.4f));
        g.drawHorizontalLine ((int) r.getY(), x0, x1);
    }

    // Sechzehntel-Raster
    g.setFont (uiFont (10.5f));
    for (int st = 0; st <= 16; ++st)
    {
        const float x = sx (st * spq * 0.25);
        if (x < r.getX() - 1 || x > r.getRight() + 1) continue;
        g.setColour (juce::Colours::white.withAlpha (st % 4 == 0 ? 0.16f : 0.06f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        if (st < 16)
        {
            g.setColour (col::textDim);
            g.drawText (juce::String (st + 1), juce::Rectangle<float> (x + 2.0f, r.getBottom() - 13.0f, 20.0f, 12.0f), juce::Justification::left);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawHorizontalLine ((int) r.getCentreY(), r.getX(), r.getRight());

    // Ereignismarker
    for (int k : rr.kickOn)
    {
        const float x = sx (k);
        if (x < r.getX() || x > r.getRight()) continue;
        g.setColour (col::kick);
        g.fillRect (x - 1.0f, r.getY(), 2.0f, 7.0f);
    }
    for (int b : rr.bassOn)
    {
        const float x = sx (b);
        if (x < r.getX() || x > r.getRight()) continue;
        g.setColour (col::bass.brighter (0.4f));
        g.fillRect (x - 1.0f, r.getBottom() - 7.0f, 2.0f, 7.0f);
    }

    // gemeinsame Skalierung (Summe ±1 entspricht Peak)
    float peak = 1.0e-4f;
    for (auto* v : { &rr.kick, &rr.bass, &rr.mix })
        for (float s : *v) peak = juce::jmax (peak, std::abs (s));
    const float scale = 0.92f / peak;

    if (showSum)  drawTrace (g, r, rr.mix,  col::sum,  scale, 1.6f);
    if (showBass) drawTrace (g, r, rr.bass, col::bass.brighter (0.3f), scale, 1.4f);
    if (showKick) drawTrace (g, r, rr.kick, col::kick, scale, 1.4f);

    // Phasen-/Kohärenzstreifen 20-120 Hz
    const auto pa = phaseArea();
    g.setColour (juce::Colour (0xff0a1430));
    g.fillRect (pa);
    const auto& fr = data->metrics.lowCoherenceFrames;
    const double hop = data->metrics.frameHopSamples;
    if (hop > 0.0)
        for (size_t i = 0; i < fr.size(); ++i)
        {
            const float x0 = juce::jmax (pa.getX(), sx ((double) i * hop)), x1 = juce::jmin (pa.getRight(), sx ((double) (i + 1) * hop));
            if (x1 <= x0) continue;
            const float c = fr[i];
            if (std::isnan (c)) continue;
            const auto colour = c < 0.0f ? col::red.withAlpha (juce::jlimit (0.15f, 1.0f, -c / 6.0f))
                                         : col::green.withAlpha (juce::jlimit (0.15f, 1.0f, c / 3.0f));
            g.setColour (colour);
            g.fillRect (x0, pa.getY(), x1 - x0 + 0.5f, pa.getHeight());
        }
    g.setColour (col::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText (T ("Phase 20–120 Hz:  grün = addieren · rot = Auslöschung · gelb = Überlappung"),
                pa.translated (0.0f, pa.getHeight()).withHeight (14.0f), juce::Justification::centredLeft);

    // Maus-Info
    if (hoverX >= r.getX() && hoverX <= r.getRight())
    {
        const double s = viewStart + (hoverX - r.getX()) / r.getWidth() * viewLen;
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawVerticalLine ((int) hoverX, r.getY(), pa.getBottom());
        juce::String t = juce::String (s / sr * 1000.0, 1) + " ms";
        const int fi = hop > 0.0 ? (int) (s / hop) : -1;
        if (fi >= 0 && fi < (int) fr.size() && ! std::isnan (fr[(size_t) fi]))
            t << T ("   Kohärenz ") << juce::String (fr[(size_t) fi], 1) << " dB";
        g.setColour (col::text);
        g.setFont (uiFont (12.0f, true));
        g.drawText (t, juce::Rectangle<float> (hoverX + 6.0f, r.getY() + 8.0f, 220.0f, 14.0f), juce::Justification::left);
    }

    // Zoom-Hinweis
    g.setColour (col::textDim.withAlpha (0.7f));
    g.setFont (uiFont (10.5f));
    g.drawText (T ("Mausrad = Zoom  ·  Ziehen = Verschieben  ·  Doppelklick = Ganzer Takt"),
                getLocalBounds().toFloat().reduced (8.0f, 2.0f).removeFromTop (14.0f).withTrimmedLeft (250.0f), juce::Justification::centredRight);
}

void ScopeView::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r)
{
    r = r.withTrimmedLeft (30.0f).withTrimmedBottom (16.0f);
    const float fLo = 20.0f, fHi = 2000.0f, dbLo = -60.0f;
    auto fx = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / fLo) / std::log (fHi / fLo); };
    auto dy = [&] (float db) { return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, db / dbLo); };

    g.setFont (uiFont (10.5f));
    for (float f : { 20.0f, 40.0f, 60.0f, 100.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f })
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine ((int) fx (f), r.getY(), r.getBottom());
        g.setColour (col::textDim);
        g.drawText (f >= 1000.0f ? juce::String (f / 1000.0f, 0) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (fx (f) - 20.0f, r.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred);
    }
    for (float db = 0.0f; db >= dbLo; db -= 12.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawHorizontalLine ((int) dy (db), r.getX(), r.getRight());
        g.setColour (col::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() - 30.0f, dy (db) - 6.0f, 26.0f, 12.0f), juce::Justification::centredRight);
    }
    // Analysebänder
    for (auto band : { std::pair<float, float> { 20.0f, 60.0f }, { 60.0f, 120.0f }, { 120.0f, 250.0f } })
    {
        g.setColour (col::amber.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fx (band.first), r.getY(), fx (band.second), r.getBottom()));
    }

    const juce::Colour cols[3] = { col::kick, col::bass.brighter (0.3f), col::sum };
    const bool vis[3] = { showKick, showBass, showSum };
    for (int c = 0; c < 3; ++c)
    {
        if (! vis[c]) continue;
        juce::Path p;
        const auto& f = data->spectrumFreqs;
        const auto& d = data->spectrumDb[(size_t) c];
        for (size_t i = 0; i < f.size() && i < d.size(); ++i)
        {
            const auto pt = juce::Point<float> (fx (f[i]), dy (d[i]));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        g.setColour (cols[c]);
        g.strokePath (p, juce::PathStrokeType (c == 2 ? 2.0f : 1.5f));
    }
}

void ScopeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (data == nullptr || mode != Mode::Wave) return;
    const auto r = waveArea();
    const double anchor = viewStart + (e.position.x - r.getX()) / r.getWidth() * viewLen;
    const double factor = std::pow (0.8, (double) w.deltaY * 4.0);
    viewLen *= factor;
    clampView();
    viewStart = anchor - (e.position.x - r.getX()) / r.getWidth() * viewLen;
    clampView();
    repaint();
}

void ScopeView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < 3; ++i)
        if (legendRect (i).contains (e.position))
        {
            bool* f[3] = { &showKick, &showBass, &showSum };
            *f[i] = ! *f[i];
            repaint();
            return;
        }
    dragStartView = viewStart;
    dragStartX = e.position.x;
}

void ScopeView::mouseDrag (const juce::MouseEvent& e)
{
    if (data == nullptr) return;
    viewStart = dragStartView - (e.position.x - dragStartX) / waveArea().getWidth() * viewLen;
    clampView();
    repaint();
}

void ScopeView::mouseDoubleClick (const juce::MouseEvent&)
{
    if (data == nullptr) return;
    viewStart = 0.0;
    viewLen = data->render.length();
    repaint();
}

void ScopeView::mouseMove (const juce::MouseEvent& e) { hoverX = e.position.x; repaint(); }

//==============================================================================
void BandMeter::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xc0030816));
    g.fillRoundedRectangle (r, 6.0f);
    if (data == nullptr) return;

    const auto& m = data->metrics;
    double top = -120.0;
    for (int j = 0; j < kNumBands; ++j)
        top = juce::jmax (top, m.kickBandDb[(size_t) j], m.bassBandDb[(size_t) j], m.sumBandDb[(size_t) j]);
    top = std::ceil (top / 6.0) * 6.0;
    const double range = 36.0;

    auto inner = r.reduced (6.0f, 4.0f);
    g.setFont (uiFont (11.0f, true));
    g.setColour (col::textDim);
    g.drawText ("Energie je Band (dB, Takt-Mittel)", inner.removeFromTop (14.0f), juce::Justification::centredLeft);
    const float colW = inner.getWidth() / (float) kNumBands;
    for (int j = 0; j < kNumBands; ++j)
    {
        auto cell = juce::Rectangle<float> (inner.getX() + colW * (float) j, inner.getY(), colW, inner.getHeight()).reduced (4.0f, 0.0f);
        g.setColour (col::textDim);
        g.setFont (uiFont (10.5f));
        g.drawText (T (kBandNames[j]), cell.removeFromBottom (13.0f), juce::Justification::centred);
        const double vals[3] = { m.kickBandDb[(size_t) j], m.bassBandDb[(size_t) j], m.sumBandDb[(size_t) j] };
        const juce::Colour cols[3] = { col::kick, col::bass.brighter (0.3f), col::sum };
        const float bw = cell.getWidth() / 3.0f;
        for (int c = 0; c < 3; ++c)
        {
            const float norm = (float) juce::jlimit (0.0, 1.0, (vals[c] - (top - range)) / range);
            auto bar = juce::Rectangle<float> (cell.getX() + bw * (float) c + 2.0f, cell.getY(), bw - 4.0f, cell.getHeight() - 14.0f);
            g.setColour (juce::Colour (0xff111c38));
            g.fillRect (bar);
            g.setColour (cols[c]);
            g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.0f - norm)));
        }
        g.setColour (col::text);
        g.setFont (uiFont (10.5f, true));
        g.drawText (T ("Σ ") + juce::String (vals[2], 1) + " dB",
                    juce::Rectangle<float> (cell.getX(), cell.getBottom() - 12.0f, cell.getWidth(), 12.0f), juce::Justification::centred);
    }
}

//==============================================================================
MixPanel::MixPanel (SamsiBasslineProcessor& p, Role r)
    : PanelFrame (r == Role::Scope ? "MIX · PHASE (KICK + BASS = SUMME)" : "MESSUNG · KEY", col::cyan),
      role (r),
      tabs (juce::StringArray { T ("MESSUNG · BÄNDER"), T ("KEY · FREQUENZ") }, col::cyan),
      keyView (p),
      monitor (p.apvts, pid::monitor, "Monitor MIX-Out")
{
    addAndMakeVisible (tabs);
    addChildComponent (keyView);
    tabs.onChange = [this] (int pg) { setPage (pg); };
    addAndMakeVisible (scope);
    addAndMakeVisible (bands);
    addAndMakeVisible (monitor);
    for (auto* b : { &liveButton, &waveButton, &specButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (101);
        addAndMakeVisible (*b);
    }
    waveButton.setToggleState (true, juce::dontSendNotification);
    monitor.setLabelLeft (104);
    waveButton.onClick = [this] { setScopeMode (1); };
    specButton.onClick = [this] { setScopeMode (2); };
    liveButton.onClick = [this] { setScopeMode (0); };
    liveButton.setTooltip (T ("LIVE: Kick (oben) und Bass (unten) als eigene Wellenform, bei jedem Schlag/jeder Note neu – "
                              "während der Wiedergabe sieht man jede Reglerbewegung sofort."));
    waveButton.setTooltip (T ("WELLE: gerenderter Takt mit Kick, Bass und Summe, Phasenstreifen und Überlappungsfenstern"));
    setPage (0);
}

void MixPanel::attachLive (const LiveAnalyzer* an, ViewOptions& opts, SamsiBasslineProcessor* p)
{
    optsPtr = &opts;
    liveKick = std::make_unique<LiveWaveView> (LiveWaveView::Kind::Kick, an, opts);
    liveBass = std::make_unique<LiveWaveView> (LiveWaveView::Kind::Bass, an, opts);
    liveKick->setProcessor (p);
    liveBass->setProcessor (p);
    addChildComponent (*liveKick);
    addChildComponent (*liveBass);
    setScopeMode (opts.get ("mix_scope_mode", 0));
    resized();
}

void MixPanel::refreshLive()
{
    if (scopeMode == 0 && liveKick != nullptr) { liveKick->refresh(); liveBass->refresh(); }
}

void MixPanel::setScopeMode (int m)
{
    scopeMode = (m == 0 && liveKick == nullptr) ? 1 : juce::jlimit (0, 2, m);
    if (optsPtr != nullptr && liveKick != nullptr) optsPtr->set ("mix_scope_mode", scopeMode);
    if (scopeMode == 1) scope.setMode (ScopeView::Mode::Wave);
    if (scopeMode == 2) scope.setMode (ScopeView::Mode::Spectrum);
    juce::Button* btn[3] = { &liveButton, &waveButton, &specButton };
    btn[scopeMode]->setToggleState (true, juce::dontSendNotification);
    const bool live = scopeMode == 0 && role == Role::Scope;
    if (role == Role::Scope) scope.setVisible (! live);
    if (liveKick != nullptr) { liveKick->setVisible (live); liveBass->setVisible (live); }
}

void MixPanel::setPreview (std::shared_ptr<const PreviewData> d, bool stale)
{
    if (d != data)
    {
        data = d;
        scope.setData (d);
        bands.setData (d);
        repaint (infoArea);
    }
    scope.setStale (stale);
}

void MixPanel::setPage (int page)
{
    const bool scopeRole = role == Role::Scope;
    const bool key = ! scopeRole && page == 1;
    tabs.setVisible (! scopeRole);
    keyView.setVisible (key);
    for (auto* c : std::initializer_list<juce::Component*> { &scope, &waveButton, &specButton, &liveButton, &monitor })
        c->setVisible (scopeRole);
    if (scopeRole) setScopeMode (scopeMode);
    bands.setVisible (! scopeRole && ! key);
    if (key) keyView.update();
    repaint();
}

void MixPanel::resized()
{
    auto r = content();
    if (role == Role::Scope)
    {
        auto buttons = r.removeFromBottom (30).reduced (0, 3);
        scope.setBounds (r.withTrimmedBottom (2));
        if (liveKick != nullptr)
        {
            auto lr = r.withTrimmedBottom (2);
            liveKick->setBounds (lr.removeFromTop (lr.getHeight() / 2).withTrimmedBottom (2));
            liveBass->setBounds (lr.withTrimmedTop (2));
        }
        liveButton.setBounds (buttons.removeFromLeft (70));
        buttons.removeFromLeft (4);
        waveButton.setBounds (buttons.removeFromLeft (80));
        buttons.removeFromLeft (4);
        specButton.setBounds (buttons.removeFromLeft (90));
        monitor.setBounds (buttons.removeFromRight (230));
        return;
    }
    tabs.setBounds (r.removeFromTop (22));
    r.removeFromTop (4);
    keyView.setBounds (r);
    bands.setBounds (r.removeFromLeft (r.getWidth() * 9 / 20).reduced (0, 4));
    infoArea = r.reduced (10, 4);
}

void MixPanel::paint (juce::Graphics& g)
{
    PanelFrame::paint (g);
    if (data == nullptr || role != Role::Measure || tabs.getSelected() != 0) return;
    const auto& m = data->metrics;

    auto r = infoArea;
    const int lineH = juce::jlimit (15, 22, infoArea.getHeight() / 6);
    auto row = [&] (const juce::String& name, const juce::String& val, juce::Colour c = col::text)
    {
        auto line = r.removeFromTop (lineH);
        g.setFont (uiFont (12.0f));
        g.setColour (col::textDim);
        g.drawText (name, line, juce::Justification::centredLeft);
        g.setFont (uiFont (12.5f, true));
        g.setColour (c);
        g.drawText (val, line, juce::Justification::centredRight);
    };
    auto dB = [] (double v) { return juce::String (v, 1) + " dB"; };
    row (T ("Phase 20–60 Hz (Ø)"), dB (m.coherenceDb[0]), m.coherenceDb[0] < -1.0 ? col::red : col::green);
    row (T ("Phase 60–120 Hz (Ø)"), dB (m.coherenceDb[1]), m.coherenceDb[1] < -1.0 ? col::red : col::green);
    row (T ("Auslöschung schlimmster Frame"), dB (-m.cancelWorstDb[0]), m.cancelWorstDb[0] > 3.0 ? col::amber : col::text);
    row ("Summenpeak", juce::String (m.sumPeakDb, 1) + " dBFS", m.sumPeakDb > -1.0 ? col::red : col::text);
    row ("Lautheit (K-gewichtet)", juce::String (m.loudnessLu, 1) + " LU");
    row ("Tempo / Samplerate", juce::String (data->render.bpm, 1) + " BPM / " + juce::String ((int) data->render.sampleRate) + " Hz", col::textDim);
}

} // namespace samsi::ui
