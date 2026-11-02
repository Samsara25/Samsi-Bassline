#include "AnalyzerView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../content/SampleLoader.h"

namespace samsi::ui
{

namespace
{
    const juce::Colour cOut  = col::chromeHi;
    const juce::Colour cKick = col::turq;
    const juce::Colour cBass = juce::Colour (0xffffd34a);   // Bass gelb wie in klassischen Kick/Bass-Scopes, gut unterscheidbar
    const juce::Colour cSc   = juce::Colour (0xff8aa0a8);
    const juce::Colour cGrid = juce::Colour (0x22a8fff4);

    juce::String noteName (int midi) { return juce::MidiMessage::getMidiNoteName (midi, true, true, 3); }   // Ableton: C3 = 60

    juce::String hzToNote (float hz)
    {
        if (hz <= 0.0f) return "-";
        const double m = 69.0 + 12.0 * std::log2 (hz / 440.0);
        const int n = (int) std::lround (m);
        const int ct = (int) std::lround ((m - n) * 100.0);
        return noteName (n) + (ct == 0 ? juce::String() : (ct > 0 ? " +" : " ") + juce::String (ct) + " ct");
    }

    juce::String dbText (float db, int dec = 1) { return db <= -119.0f ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (db, dec); }

    void plotPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xd0010506));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (col::cyan.withAlpha (0.18f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    /** Zeichnet eine Spur; bei vielen Samples je Pixel als Min/Max-Hülle. */
    template <typename Fn>
    void drawWave (juce::Graphics& g, juce::Rectangle<float> r, int count, Fn sampleAt, float scale, juce::Colour c, float thick)
    {
        if (count < 2) return;
        const int W = std::max (2, (int) r.getWidth());
        const float cy = r.getCentreY(), hh = r.getHeight() * 0.5f * 0.94f;
        juce::Path p;
        if (count > W * 2)
        {
            juce::Path env;
            std::vector<float> lo ((size_t) W), hi ((size_t) W);
            for (int x = 0; x < W; ++x)
            {
                const int a = (int) ((int64_t) x * count / W), b = std::max (a + 1, (int) ((int64_t) (x + 1) * count / W));
                float mn = 1.0e9f, mx = -1.0e9f;
                for (int i = a; i < b; ++i) { const float v = sampleAt (i); mn = std::min (mn, v); mx = std::max (mx, v); }
                lo[(size_t) x] = mn; hi[(size_t) x] = mx;
            }
            for (int x = 0; x < W; ++x)
            {
                const float px = r.getX() + (float) x, py = cy - juce::jlimit (-1.0f, 1.0f, hi[(size_t) x] * scale) * hh;
                if (x == 0) env.startNewSubPath (px, py); else env.lineTo (px, py);
            }
            for (int x = W - 1; x >= 0; --x)
                env.lineTo (r.getX() + (float) x, cy - juce::jlimit (-1.0f, 1.0f, lo[(size_t) x] * scale) * hh);
            env.closeSubPath();
            g.setColour (c.withMultipliedAlpha (0.75f));
            g.fillPath (env);
            g.setColour (c);
            g.strokePath (env, juce::PathStrokeType (0.6f));
            return;
        }
        for (int i = 0; i < count; ++i)
        {
            const float px = r.getX() + r.getWidth() * (float) i / (float) (count - 1);
            const float py = cy - juce::jlimit (-1.0f, 1.0f, sampleAt (i) * scale) * hh;
            if (i == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        g.setColour (c.withMultipliedAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (thick + 2.5f, juce::PathStrokeType::curved));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved));
    }

    juce::Colour paletteColour (int pal, float t)
    {
        static const std::vector<std::vector<juce::uint32>> pals {
            { 0xff050a26, 0xff1d3fbf, 0xffb23bd9, 0xffff8a3a, 0xffffffff },
            { 0xff000000, 0xff1430c8, 0xff20d8f0, 0xff30e070, 0xfff0e840, 0xffff9020, 0xffff3020 },
            { 0xff000000, 0xff18b040, 0xffe03030, 0xffff9a20, 0xfffff060 },
            { 0xff000000, 0xff0a1f5c, 0xff2f7bff, 0xff8fd0ff, 0xfff0fbff },
            { 0xff000000, 0xff4a148c, 0xffd03030, 0xfff0d040, 0xffffffff },
            { 0xff04040a, 0xff2040d0, 0xff7a30c8, 0xffe050b0, 0xffff4040, 0xffff9a30, 0xfffff060, 0xffffffff } };
        const auto& p = pals[(size_t) juce::jlimit (0, (int) pals.size() - 1, pal)];
        t = juce::jlimit (0.0f, 1.0f, t) * (float) (p.size() - 1);
        const int i = std::min ((int) p.size() - 2, (int) t);
        return juce::Colour (p[(size_t) i]).interpolatedWith (juce::Colour (p[(size_t) i + 1]), t - (float) i);
    }
}

//==============================================================================
Pills::Pills (juce::StringArray it, juce::Colour a, bool m) : items (std::move (it)), accent (a), multi (m) {}

int Pills::idealWidth() const
{
    int w = 0;
    for (auto& s : items) w += juce::GlyphArrangement::getStringWidthInt (uiFont (11.5f, true), s) + 16;
    return w;
}

void Pills::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xa0031014));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (accent.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
    const int total = idealWidth();
    float x = r.getX();
    for (int i = 0; i < items.size(); ++i)
    {
        const float w = r.getWidth() * (float) (juce::GlyphArrangement::getStringWidthInt (uiFont (11.5f, true), items[i]) + 16) / (float) std::max (1, total);
        auto cell = juce::Rectangle<float> (x, r.getY(), w, r.getHeight()).reduced (2.0f);
        const bool on = multi ? ((mask >> i) & 1) != 0 : i == selected;
        if (on)
        {
            g.setColour (accent.withAlpha (0.28f));
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
        }
        g.setColour (on ? col::text : col::textDim);
        g.setFont (uiFont (11.5f, true));
        g.drawText (items[i], cell, juce::Justification::centred);
        x += w;
    }
}

void Pills::mouseDown (const juce::MouseEvent& e)
{
    const int total = idealWidth();
    float x = 0.0f;
    for (int i = 0; i < items.size(); ++i)
    {
        const float w = (float) getWidth() * (float) (juce::GlyphArrangement::getStringWidthInt (uiFont (11.5f, true), items[i]) + 16) / (float) std::max (1, total);
        if (e.position.x >= x && e.position.x < x + w)
        {
            if (multi) setMask (mask ^ (1 << i), true);
            else setSelected (i, true);
            return;
        }
        x += w;
    }
}

void Pills::setSelected (int i, bool notify)
{
    selected = juce::jlimit (0, items.size() - 1, i);
    repaint();
    if (notify && onChange) onChange (selected);
}

void Pills::setMask (int m, bool notify)
{
    mask = m;
    repaint();
    if (notify && onChange) onChange (mask);
}

//==============================================================================
CycleButton::CycleButton (juce::String p, juce::StringArray v, int initial, juce::Colour a)
    : prefix (std::move (p)), values (std::move (v)), index (juce::jlimit (0, values.size() - 1, initial)), accent (a)
{
    setTooltip (T ("Klick = nächster Wert, Rechtsklick = vorheriger"));
}

int CycleButton::idealWidth() const
{
    int w = 0;
    for (auto& v : values) w = std::max (w, juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f, true), prefix + " " + v));
    return w + 16;
}

void CycleButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xa0031014));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (accent.withAlpha (0.4f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setFont (uiFont (11.0f, true));
    g.setColour (col::textDim);
    const auto pw = (float) juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f, true), prefix + " ");
    const float tw = pw + (float) juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f, true), values[index]);
    const float x0 = r.getCentreX() - tw * 0.5f;
    g.drawText (prefix, juce::Rectangle<float> (x0, r.getY(), pw, r.getHeight()), juce::Justification::centredLeft);
    g.setColour (col::text);
    g.drawText (values[index], juce::Rectangle<float> (x0 + pw, r.getY(), tw - pw + 2.0f, r.getHeight()), juce::Justification::centredLeft);
}

void CycleButton::mouseDown (const juce::MouseEvent& e)
{
    const int n = values.size();
    index = e.mods.isPopupMenu() ? (index + n - 1) % n : (index + 1) % n;
    repaint();
    if (onChange) onChange (index);
}

//==============================================================================
LiveWaveView::LiveWaveView (Kind k, const LiveAnalyzer* a, ViewOptions& o) : kind (k), an (a), opts (o)
{
    zoomMs = (float) opts.get (k == Kind::Kick ? "lw_kick_ms" : "lw_bass_ms", k == Kind::Kick ? 300 : 150);
    autoGain = opts.get ("lw_autogain", 1) != 0;
    trails = opts.get ("lw_trails", 1) != 0;
    setTooltip (T ("Live-Wellenform: jeder neue Schlag bzw. jede neue Note wird ab ihrem Start mitgeschnitten. "
                   "Hell = jetzt, blasser = vorherige (Nachleuchten).\n"
                   "Eigenes Sample hierher ziehen: wird geladen, analysiert, phasen-ausgerichtet und (wählbar) per Auto Fit abgestimmt.\n"
                   "Mausrad = Zeitbereich, Klick = Nachleuchten, Doppelklick = Auto-Gain, Rechtsklick = Menü (Laden, Ausrichten, Auto Fit, Entfernen)"));
}

void LiveWaveView::refresh()
{
    const auto* c = an == nullptr ? nullptr : (kind == Kind::Kick ? an->kickCapture (0, false) : an->bassCapture (0, false));
    const int64_t s = c != nullptr ? c->serial : -1;
    const int len = c != nullptr ? c->length : -1;
    const bool msg = message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 8500;
    if (s != lastSerial || len != lastLen || msg) { lastSerial = s; lastLen = len; repaint(); }
}

void LiveWaveView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float maxMs = kind == Kind::Kick ? 400.0f : 250.0f;
    zoomMs = juce::jlimit (5.0f, maxMs, zoomMs * (w.deltaY > 0 ? 0.85f : 1.0f / 0.85f));
    opts.set (kind == Kind::Kick ? "lw_kick_ms" : "lw_bass_ms", (int) zoomMs);
    repaint();
}

bool LiveWaveView::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (proc == nullptr) return false;
    for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void LiveWaveView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) { loadFile (juce::File (f)); break; }
    repaint();
}

juce::String LiveWaveView::loadFile (const juce::File& f)
{
    if (proc == nullptr) return {};
    const bool isKick = kind == Kind::Kick;
    juce::String err = isKick ? proc->loadKickSample (f) : proc->loadBassSample (f);
    messageError = err.isNotEmpty();
    if (! messageError)
    {
        if (isKick)
        {
            if (auto* prm = proc->apvts.getParameter (pid::ksOn)) prm->setValueNotifyingHost (1.0f);
            const auto r = proc->getLastLoadAlign();     // beim Laden automatisch: Phase/Zeit/Polarität an die Synth-Kick
            auto k = proc->getKickSample();
            err = f.getFileName() + (k != nullptr && k->pitchHz > 0.0f ? T (" · ") + juce::String (k->pitchHz, 1) + " Hz" : juce::String())
                  + (r.valid ? T (" · ausgerichtet, Korrelation ") + juce::String (r.coherenceBefore, 2) + T (" → ") + juce::String (r.coherenceAfter, 2)
                                   + (r.invert ? T (" (invertiert)") : juce::String()) : juce::String());
        }
        else
        {
            auto b = proc->getBassSample();
            err = f.getFileName() + (b != nullptr && b->pitchHz > 0.0f ? T (" · Grundton ") + juce::String (b->pitchHz, 1) + " Hz, pro Note gestimmt"
                                                                       : T (" · kein klarer Grundton – Stimmung über Tune"));
        }
        if (opts.get ("lw_autofit", 1) != 0)
        {
            proc->startAutoFit();
            err << T (" · Auto Fit läuft");
        }
    }
    message = err;
    messageTime = juce::Time::getMillisecondCounter();
    repaint();
    return message;
}

void LiveWaveView::showMenu()
{
    if (proc == nullptr) return;
    const bool isKick = kind == Kind::Kick;
    const bool has = isKick ? proc->getKickSample() != nullptr : proc->getBassSample() != nullptr;
    juce::PopupMenu m;
    m.addSectionHeader (isKick ? T ("KICK-SAMPLE") : T ("BASS-SAMPLE"));
    m.addItem (1, T ("Sample laden …"));
    m.addItem (2, T ("Sample entfernen"), has);
    if (isKick) m.addItem (3, T ("Sample jetzt ausrichten (Phase, Zeit, Polarität)"), has);
    m.addItem (4, T ("Auto Fit starten (Kick + Bass abstimmen)"));
    m.addSeparator();
    m.addItem (5, T ("Nach dem Ablegen automatisch Auto Fit"), true, opts.get ("lw_autofit", 1) != 0);
    m.addItem (6, T ("Sample als Referenz einblenden"), true, opts.get ("lw_ref", 1) != 0);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, isKick] (int r)
    {
        if (proc == nullptr) return;
        if (r == 1)
        {
            chooser = std::make_unique<juce::FileChooser> (isKick ? "Kick-Sample laden" : "Bass-Sample laden", juce::File(), kAudioFilePatterns);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
        }
        if (r == 2) { if (isKick) proc->clearKickSample(); else proc->clearBassSample(); message = T ("Sample entfernt"); messageError = false; messageTime = juce::Time::getMillisecondCounter(); }
        if (r == 3)
        {
            const auto a = proc->alignKickSampleNow();
            message = a.valid ? T ("Ausgerichtet: Korrelation ") + juce::String (a.coherenceBefore, 2) + T (" → ") + juce::String (a.coherenceAfter, 2) : T ("Kein Sample");
            messageError = ! a.valid; messageTime = juce::Time::getMillisecondCounter();
        }
        if (r == 4) { proc->startAutoFit(); message = T ("Auto Fit läuft …"); messageError = false; messageTime = juce::Time::getMillisecondCounter(); }
        if (r == 5) opts.set ("lw_autofit", 1 - opts.get ("lw_autofit", 1));
        if (r == 6) opts.set ("lw_ref", 1 - opts.get ("lw_ref", 1));
        repaint();
    });
}

void LiveWaveView::drawSampleReference (juce::Graphics& g, juce::Rectangle<float> plot, int span, float scale)
{
    if (proc == nullptr || opts.get ("lw_ref", 1) == 0) return;
    const bool isKick = kind == Kind::Kick;
    auto smp = isKick ? proc->getKickSample() : proc->getBassSample();
    if (smp == nullptr || smp->data.empty()) return;
    const auto s = proc->readSettings();
    const double sr = an != nullptr ? an->getSampleRate() : 48000.0;
    double rate = smp->rate / sr, startS = 0.0, delayS = 0.0;
    float gain = 1.0f;
    if (isKick)
    {
        if (! s.kick.smpOn) return;
        rate *= std::pow (2.0, s.kick.smpTuneSt / 12.0);
        startS = s.kick.smpStartMs * 0.001 * smp->rate;
        delayS = s.kick.smpOffsetMs * 0.001 * sr;
        gain = juce::Decibels::decibelsToGain (s.kick.smpLevelDb) * (s.kick.smpInvert ? -1.0f : 1.0f);
    }
    else
    {
        if (s.bass.smpLevel < 0.01f) return;
        // Bass-Sample wird pro Note gestimmt: auf die Tonhöhe der zuletzt gespielten Note abbilden
        const auto* c = an != nullptr ? an->bassCapture (0, true) : nullptr;
        if (c != nullptr && c->pitchHz > 0.0f && smp->pitchHz > 0.0f) rate *= c->pitchHz / smp->pitchHz;
        rate *= std::pow (2.0, s.bass.smpTuneSt / 12.0);
        startS = s.bass.smpStartMs * 0.001 * smp->rate;
        gain = s.bass.smpLevel;
    }
    const auto& d = smp->data;
    auto at = [&] (int i)
    {
        const double t = (double) i - delayS;
        if (t < 0.0) return 0.0f;
        const double pos = startS + t * rate;
        const int k = (int) pos;
        if (k + 1 >= (int) d.size()) return 0.0f;
        const float fr = (float) (pos - k);
        return gain * (d[(size_t) k] + fr * (d[(size_t) k + 1] - d[(size_t) k]));
    };
    drawWave (g, plot, span, at, scale, juce::Colours::white.withAlpha (0.35f), 0.9f);
    g.setFont (uiFont (9.5f, true));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawText ("SAMPLE: " + juce::String::fromUTF8 (smp->name.c_str()), plot.toNearestInt().removeFromTop (12).translated (2, 0), juce::Justification::topLeft);
}

void LiveWaveView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    if (e.getNumberOfClicks() > 1) return;
    trails = ! trails;
    opts.set ("lw_trails", trails ? 1 : 0);
    repaint();
}

void LiveWaveView::mouseDoubleClick (const juce::MouseEvent&)
{
    trails = ! trails;   // Einzelklick davor rückgängig machen
    autoGain = ! autoGain;
    opts.set ("lw_trails", trails ? 1 : 0);
    opts.set ("lw_autogain", autoGain ? 1 : 0);
    repaint();
}

void LiveWaveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    plotPanel (g, r);
    const bool isKick = kind == Kind::Kick;
    const juce::Colour c = isKick ? cKick : cBass;
    auto head = r.removeFromTop (20.0f).reduced (8.0f, 2.0f);
    auto plot = r.reduced (6.0f, 4.0f).withTrimmedBottom (12.0f);

    // Raster: Mittellinie, Zeitmarken
    g.setColour (cGrid);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    const float stepMs = zoomMs > 200 ? 50.0f : zoomMs > 80 ? 20.0f : zoomMs > 30 ? 10.0f : zoomMs > 12 ? 2.0f : 1.0f;
    g.setFont (uiFont (9.5f));
    for (float t = 0.0f; t <= zoomMs + 0.01f; t += stepMs)
    {
        const float x = plot.getX() + plot.getWidth() * t / zoomMs;
        g.setColour (cGrid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (col::textDim.withAlpha (0.7f));
        g.drawText (juce::String ((int) t) + " ms", juce::Rectangle<float> (x - 22.0f, plot.getBottom() + 1.0f, 44.0f, 11.0f), juce::Justification::centred);
    }

    const auto* now = an == nullptr ? nullptr : (isKick ? an->kickCapture (0, false) : an->bassCapture (0, false));
    const auto* done = an == nullptr ? nullptr : (isKick ? an->kickCapture (0, true) : an->bassCapture (0, true));
    const double sr = an != nullptr ? an->getSampleRate() : 48000.0;
    const int span = std::max (2, (int) (zoomMs * 0.001 * sr));

    g.setFont (uiFont (12.0f, true));
    g.setColour (c);
    g.drawText (isKick ? "KICK" : "BASS", head, juce::Justification::centredLeft);
    auto overlays = [&]
    {
        if (message.isNotEmpty() && juce::Time::getMillisecondCounter() - messageTime < 8000)
        {
            auto box = plot.withTrimmedTop (plot.getHeight() - 18.0f).reduced (2.0f, 0.0f);
            g.setColour (juce::Colour (0xd0020809));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (messageError ? col::red : col::cyan);
            g.setFont (uiFont (10.5f, true));
            g.drawFittedText (message, box.toNearestInt().reduced (4, 0), juce::Justification::centredLeft, 1, 0.7f);
        }
        if (dragOver)
        {
            g.setColour (c.withAlpha (0.18f));
            g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f);
            g.setColour (c);
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 6.0f, 2.0f);
            g.setFont (uiFont (14.0f, true));
            g.drawFittedText (isKick ? T ("Ablegen → Kick-Sample (wird phasen-ausgerichtet)") : T ("Ablegen → Bass-Sample (pro Note gestimmt, Auto Fit)"),
                              getLocalBounds().reduced (8), juce::Justification::centred, 2);
        }
    };
    if (now == nullptr)
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (11.5f));
        g.drawFittedText (isKick ? T ("Wiedergabe starten: jeder Kick wird live mitgeschnitten · eigenes Kick-Sample hierher ziehen")
                                 : T ("Wiedergabe starten: jede Bassnote wird live mitgeschnitten · eigenes Bass-Sample hierher ziehen"),
                          plot.toNearestInt(), juce::Justification::centred, 3);
        drawSampleReference (g, plot, span, 1.0f);
        overlays();
        return;
    }
    if (done != nullptr)
    {
        juce::String info = (done->pitchHz > 0.0f ? juce::String (done->pitchHz, 1) + " Hz (" + hzToNote (done->pitchHz) + ")" : T ("–"))

                            + (done->slide ? T ("  ·  Slide") : juce::String());
        g.setFont (uiFont (11.5f));
        g.setColour (col::text);
        g.drawText (info, head, juce::Justification::centredRight);
    }

    // Skalierung: Maximum aller sichtbaren Mitschnitte
    float pk = 1.0e-4f;
    for (int i = 0; i < LiveAnalyzer::kCaptures; ++i)
        if (const auto* cp = isKick ? an->kickCapture (i, false) : an->bassCapture (i, false))
            for (int k = 0; k < std::min (cp->length, span); ++k) pk = std::max (pk, std::abs (cp->data[(size_t) k]));
    const float scale = autoGain ? 1.0f / pk : 1.0f;

    // Nachleuchten (älteste zuerst)
    if (trails)
        for (int i = LiveAnalyzer::kCaptures - 1; i >= 1; --i)
            if (const auto* cp = isKick ? an->kickCapture (i, false) : an->bassCapture (i, false))
            {
                const float alpha = 0.10f + 0.30f * (float) (LiveAnalyzer::kCaptures - i) / (float) LiveAnalyzer::kCaptures;
                const int n = std::min (cp->length, span);
                const float w = plot.getWidth() * (float) n / (float) span;
                drawWave (g, plot.withWidth (std::max (2.0f, w)), n, [cp] (int k) { return cp->data[(size_t) k]; }, scale, c.withAlpha (alpha), 1.0f);
            }
    // geladenes Sample als Referenz (so wie es abgespielt wird: Start, Verzögerung, Stimmung, Polarität)
    drawSampleReference (g, plot, span, scale);
    // aktueller Mitschnitt (wächst, solange er läuft)
    const int n = std::min (now->length, span);
    const float w = plot.getWidth() * (float) n / (float) span;
    drawWave (g, plot.withWidth (std::max (2.0f, w)), n, [now] (int k) { return now->data[(size_t) k]; }, scale, c.brighter (0.25f), 1.6f);
    overlays();

    if (plot.getHeight() > 110.0f)
    {
        g.setFont (uiFont (9.5f));
        g.setColour (col::textDim);
        g.drawText ((autoGain ? "AUTO-GAIN" : "0 dBFS") + juce::String (trails ? T ("  ·  NACHLEUCHTEN") : juce::String()),
                    plot.toNearestInt().removeFromBottom (12).translated (-2, 0), juce::Justification::bottomRight);
    }
}

//==============================================================================
LiveScope::LiveScope (const LiveAnalyzer* a, ViewOptions& o)
    : an (a), opts (o),
      trigger ({ "SIGNAL", "BEAT" }, col::cyan),
      layout ({ "MONO", "STEREO" }, col::cyan),
      traces ({ "OUT", "KICK", "BASS", "SC" }, col::cyan, true),
      gain ("GAIN", { "AUTO", "0 dBFS" }, opts.get ("sc_gain", 0), col::cyan)
{
    trigger.setSelected (opts.get ("sc_trig", 1));
    layout.setSelected (opts.get ("sc_layout", 0));
    traces.setMask (opts.get ("sc_traces", 0b0111));
    windowMs = (float) opts.get ("sc_win", 50);
    trigger.setTooltip (T ("SIGNAL: startet am Kick (bzw. an einer steigenden Flanke), 5–200 ms.\n"
                           "BEAT: ein Viertel bei Host-Tempo (0 – 1/2 – 1 Beat), Mausrad zoomt bis ~5 ms. "
                           "Läuft der Host nicht, folgt BEAT dem Signal-Trigger."));
    traces.setTooltip (T ("Spuren: OUT = MIX-Ausgang, KICK, BASS, SC = Sidechain-Eingang (grau, nur Anzeige)"));
    trigger.onChange = [this] (int v) { opts.set ("sc_trig", v); repaint(); };
    layout.onChange = [this] (int v) { opts.set ("sc_layout", v); repaint(); };
    traces.onChange = [this] (int v) { opts.set ("sc_traces", v); repaint(); };
    gain.onChange = [this] (int v) { opts.set ("sc_gain", v); repaint(); };
    for (auto* c : std::initializer_list<juce::Component*> { &trigger, &layout, &traces, &gain }) addAndMakeVisible (c);
}

void LiveScope::resized()
{
    auto r = getLocalBounds().removeFromTop (22);
    trigger.setBounds (r.removeFromLeft (trigger.idealWidth() + 10));
    r.removeFromLeft (8);
    layout.setBounds (r.removeFromLeft (layout.idealWidth() + 10));
    r.removeFromLeft (8);
    traces.setBounds (r.removeFromLeft (traces.idealWidth() + 14));
    r.removeFromLeft (8);
    gain.setBounds (r.removeFromLeft (gain.idealWidth()));
}

juce::Rectangle<int> LiveScope::plotArea() const { return getLocalBounds().withTrimmedTop (28).withTrimmedBottom (14); }

void LiveScope::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const float f = w.deltaY > 0 ? 0.85f : 1.0f / 0.85f;
    if (trigger.getSelected() == 1 && an != nullptr && an->beatValid())
        beatZoom = juce::jlimit (0.02f, 1.0f, beatZoom * f);
    else
    {
        windowMs = juce::jlimit (5.0f, 200.0f, windowMs * f);
        for (float snap : { 20.0f, 50.0f, 100.0f, 150.0f, 200.0f }) if (std::abs (windowMs - snap) < snap * 0.06f) windowMs = snap;
        opts.set ("sc_win", (int) windowMs);
    }
    repaint();
}

void LiveScope::mouseDoubleClick (const juce::MouseEvent&)
{
    windowMs = 50.0f; beatZoom = 1.0f;
    opts.set ("sc_win", 50);
    repaint();
}

void LiveScope::paint (juce::Graphics& g)
{
    const auto area = plotArea().toFloat();
    plotPanel (g, area);
    if (an == nullptr) return;
    const bool stereo = layout.getSelected() == 1;
    const int mask = traces.getMask();
    const bool beatMode = trigger.getSelected() == 1 && an->beatValid();
    const double sr = an->getSampleRate();

    // Spuren-Zugriff je Modus
    struct Src { int count; std::function<float (int, int)> at; };   // (trace, index)
    Src src;
    int trigAgo = -1;
    if (beatMode)
    {
        const int bins = std::max (8, (int) ((float) LiveAnalyzer::kBeatBins * beatZoom));
        src = { bins, [this] (int t, int i) { return an->beatTrace (t)[(size_t) i]; } };
    }
    else
    {
        const int win = std::max (16, (int) (windowMs * 0.001 * sr));
        trigAgo = an->findTrigger (LiveAnalyzer::MixL, win);
        if (trigAgo < 0) trigAgo = win;
        src = { win, [this, trigAgo] (int t, int i) { return an->hist (t, trigAgo - i); } };
    }

    // Skalierung
    float pk = 1.0e-4f;
    const int step = std::max (1, src.count / 2000);
    for (int t = 0; t < LiveAnalyzer::NumTraces; ++t)
        for (int i = 0; i < src.count; i += step) pk = std::max (pk, std::abs (src.at (t, i)));
    const float scale = gain.getIndex() == 0 ? 0.95f / pk : 1.0f;

    auto drawLane = [&] (juce::Rectangle<float> lane, int ch)   // ch: 0 = L, 1 = R, -1 = Mono
    {
        g.setColour (cGrid);
        g.drawHorizontalLine ((int) lane.getCentreY(), lane.getX(), lane.getRight());
        auto pick = [&] (int traceL, int traceR, int i)
        {
            if (ch == 0) return src.at (traceL, i);
            if (ch == 1) return src.at (traceR, i);
            return 0.5f * (src.at (traceL, i) + src.at (traceR, i));
        };
        if ((mask & 8) != 0)
            drawWave (g, lane, src.count, [&] (int i) { return pick (LiveAnalyzer::ScL, LiveAnalyzer::ScR, i); }, scale,
                      cSc.withAlpha (0.55f), 1.0f);
        if ((mask & 2) != 0)
            drawWave (g, lane, src.count, [&] (int i) { return src.at (LiveAnalyzer::Kick, i); }, scale, cKick.withAlpha (0.9f), 1.4f);
        if ((mask & 4) != 0)
            drawWave (g, lane, src.count, [&] (int i) { return src.at (LiveAnalyzer::Bass, i); }, scale, cBass.withAlpha (0.9f), 1.4f);
        if ((mask & 1) != 0)
            drawWave (g, lane, src.count, [&] (int i) { return pick (LiveAnalyzer::MixL, LiveAnalyzer::MixR, i); }, scale, cOut, 1.2f);
        g.setColour (col::textDim);
        g.setFont (uiFont (10.0f, true));
        if (ch >= 0) g.drawText (ch == 0 ? "L" : "R", lane.removeFromLeft (16.0f).removeFromTop (14.0f).translated (4.0f, 2.0f), juce::Justification::topLeft);
    };

    // Zeitmarken
    const auto inner = area.reduced (4.0f, 4.0f);
    g.setFont (uiFont (10.0f));
    if (beatMode)
    {
        for (int k = 0; k <= 8; ++k)
        {
            const float frac = (float) k / 8.0f;
            if (frac > beatZoom + 1.0e-4f) break;
            const float x = inner.getX() + inner.getWidth() * frac / beatZoom;
            g.setColour (k % 4 == 0 ? col::cyan.withAlpha (0.35f) : cGrid);
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            g.setColour (col::textDim);
            const juce::String lab = k == 0 ? "0" : k == 4 ? "1/2" : k == 8 ? "1 beat" : (k % 2 == 0 ? juce::String (k / 2) + "/4" : juce::String());
            if (lab.isNotEmpty()) g.drawText (lab, juce::Rectangle<float> (juce::jlimit (area.getX(), area.getRight() - 60.0f, x - 30.0f), area.getBottom(), 60.0f, 13.0f),
                                              k == 0 ? juce::Justification::centredLeft : k == 8 && beatZoom >= 0.99f ? juce::Justification::centredRight : juce::Justification::centred);
        }
    }
    else
    {
        const float stepMs = windowMs > 100 ? 25.0f : windowMs > 40 ? 10.0f : windowMs > 15 ? 5.0f : 1.0f;
        for (float t = 0.0f; t <= windowMs + 0.01f; t += stepMs)
        {
            const float x = inner.getX() + inner.getWidth() * t / windowMs;
            g.setColour (cGrid);
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            g.setColour (col::textDim);
            g.drawText (juce::String ((int) t) + " ms", juce::Rectangle<float> (juce::jlimit (area.getX(), area.getRight() - 60.0f, x - 30.0f), area.getBottom(), 60.0f, 13.0f), juce::Justification::centred);
        }
        g.setColour (col::amber);
        g.drawText ("T", juce::Rectangle<float> (inner.getX() + 2.0f, inner.getY(), 12.0f, 12.0f), juce::Justification::centred);
    }

    if (stereo)
    {
        auto top = inner.withHeight (inner.getHeight() * 0.5f);
        drawLane (top, 0);
        drawLane (top.translated (0.0f, inner.getHeight() * 0.5f), 1);
    }
    else
        drawLane (inner, -1);

    // Kopfzeile rechts: Modus, Fenster
    g.setFont (uiFont (11.0f));
    g.setColour (col::textDim);
    juce::String s = beatMode ? "BEAT " + juce::String (juce::roundToInt (beatZoom * 100.0f)) + " %"
                              : (trigger.getSelected() == 1 ? T ("BEAT (Host steht → Signal)  ") : juce::String())
                                    + juce::String (windowMs, 0) + " ms";
    g.drawText (s, getLocalBounds().removeFromTop (22).withTrimmedRight (4), juce::Justification::centredRight);
}

//==============================================================================
SpectrumView::SpectrumView (LiveAnalyzer* a, ViewOptions& o)
    : an (a), opts (o),
      mode ({ "LINIE", "OKTAV", "SPEKTROGRAMM" }, col::cyan),
      source ({ "MIX", "KICK+BASS", "KICK", "BASS" }, col::cyan),
      fftSize ("FFT", { "1024", "2048", "4096", "8192", "16384" }, opts.get ("sp_fft", 3), col::cyan),
      window ("FENSTER", { "Hann", "Hamming", "Blackman-H.", "Flattop", "Rechteck" }, opts.get ("sp_win", 0), col::cyan),
      smooth (T ("GLÄTTUNG"), { "aus", "1/6", "1/12", "1/24", "1/48" }, opts.get ("sp_smooth", 4), col::cyan),
      tilt ("NEIGUNG", { "0", "1,5", "3", "4,5", "6" }, opts.get ("sp_tilt", 0), col::cyan),
      range ("BEREICH", { "-120", "-90", "-60", "-30" }, opts.get ("sp_range", 1), col::cyan),
      orient ("RICHTUNG", { "horizontal", "vertikal" }, opts.get ("sp_orient", 0), col::cyan),
      palette ("PALETTE", { "1", "2", "3", "4", "5", "6" }, opts.get ("sp_pal", 0), col::cyan)
{
    mode.setSelected (opts.get ("sp_mode", 0));
    source.setSelected (opts.get ("sp_src", 0));
    rootNote = opts.get ("sp_root", 0);
    mode.onChange = [this] (int v) { opts.set ("sp_mode", v); resized(); repaint(); };
    source.onChange = [this] (int v) { opts.set ("sp_src", v); repaint(); };
    fftSize.onChange = [this] (int v) { opts.set ("sp_fft", v); };
    window.onChange = [this] (int v) { opts.set ("sp_win", v); };
    smooth.onChange = [this] (int v) { opts.set ("sp_smooth", v); };
    tilt.onChange = [this] (int v) { opts.set ("sp_tilt", v); };
    range.onChange = [this] (int v) { opts.set ("sp_range", v); gram = {}; };
    orient.onChange = [this] (int v) { opts.set ("sp_orient", v); gram = {}; };
    palette.onChange = [this] (int v) { opts.set ("sp_pal", v); gram = {}; };
    for (auto* b : { &holdButton, &gridButton, &scButton })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (*b);
    }
    holdButton.setToggleState (opts.get ("sp_hold", 0) != 0, juce::dontSendNotification);
    gridButton.setToggleState (opts.get ("sp_grid", 1) != 0, juce::dontSendNotification);
    scButton.setToggleState (opts.get ("sp_sc", 1) != 0, juce::dontSendNotification);
    holdButton.onClick = [this] { opts.set ("sp_hold", holdButton.getToggleState() ? 1 : 0); std::fill (hold.begin(), hold.end(), -200.0f); };
    gridButton.onClick = [this] { opts.set ("sp_grid", gridButton.getToggleState() ? 1 : 0); repaint(); };
    scButton.onClick = [this] { opts.set ("sp_sc", scButton.getToggleState() ? 1 : 0); repaint(); };
    holdButton.setTooltip (T ("Spitzen halten (Doppelklick in die Anzeige = zurücksetzen)"));
    gridButton.setTooltip (T ("Musikalisches Raster: C-Linien mit Notennamen (Ableton: C3 = 60). Umschalt + Mausrad = Grundton"));
    scButton.setTooltip (T ("Sidechain-Kurve einblenden (grau)"));
    fftSize.setTooltip (T ("FFT-Größe: größer = feinere Frequenzauflösung im Bass, langsamere Reaktion"));
    window.setTooltip (T ("Fensterfunktion: Hann (Standard), Flattop für exakte Pegel, Blackman-Harris für hohe Dynamik"));
    for (auto* c : std::initializer_list<juce::Component*> { &mode, &source, &fftSize, &window, &smooth, &tilt, &range, &orient, &palette })
        addAndMakeVisible (c);
}

void SpectrumView::resized()
{
    auto r = getLocalBounds().removeFromTop (22);
    mode.setBounds (r.removeFromLeft (mode.idealWidth() + 12));
    r.removeFromLeft (8);
    source.setBounds (r.removeFromLeft (source.idealWidth() + 12));
    r.removeFromLeft (8);
    scButton.setBounds (r.removeFromLeft (36));
    r.removeFromLeft (6);
    holdButton.setBounds (r.removeFromLeft (84));
    r.removeFromLeft (6);
    gridButton.setBounds (r.removeFromLeft (70));

    auto b = getLocalBounds().removeFromBottom (22);
    const bool gramMode = mode.getSelected() == 2;
    for (auto* c : { &fftSize, &window, &smooth, &tilt, &range, &orient, &palette })
    {
        const bool vis = (c != &orient && c != &palette) || gramMode;
        const bool notForGram = c == &smooth || c == &tilt;
        c->setVisible (vis && ! (gramMode && notForGram));
        if (c->isVisible()) { c->setBounds (b.removeFromLeft (c->idealWidth())); b.removeFromLeft (6); }
    }
    gram = {};
}

juce::Rectangle<int> SpectrumView::plotArea() const { return getLocalBounds().withTrimmedTop (28).withTrimmedBottom (28); }

float SpectrumView::hzToX (float hz, juce::Rectangle<float> r) const
{
    const float lo = std::log (10.0f), hi = std::log (22000.0f);
    return r.getX() + r.getWidth() * (std::log (std::max (1.0f, hz)) - lo) / (hi - lo);
}

float SpectrumView::xToHz (float x, juce::Rectangle<float> r) const
{
    const float lo = std::log (10.0f), hi = std::log (22000.0f);
    return std::exp (lo + (hi - lo) * (x - r.getX()) / std::max (1.0f, r.getWidth()));
}

float SpectrumView::dbToY (float db, juce::Rectangle<float> r) const
{
    const float rng = (float) std::abs (range.getIndex() == 0 ? 120 : range.getIndex() == 1 ? 90 : range.getIndex() == 2 ? 60 : 30);
    return r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -db / rng);
}

void SpectrumView::mouseMove (const juce::MouseEvent& e) { hover = e.position; repaint(); }

void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isShiftDown())
    {
        rootNote = (rootNote + (w.deltaY > 0 ? 1 : 11)) % 12;
        opts.set ("sp_root", rootNote);
        repaint();
    }
}

void SpectrumView::computeCurve (int src, std::vector<float>& curve)
{
    static const int orders[] = { 10, 11, 12, 13, 14 };
    static const LiveAnalyzer::Window wins[] = { LiveAnalyzer::Window::Hann, LiveAnalyzer::Window::Hamming,
        LiveAnalyzer::Window::BlackmanHarris, LiveAnalyzer::Window::Flattop, LiveAnalyzer::Window::Rectangular };
    const int order = orders[fftSize.getIndex()];
    an->computeSpectrum (src, order, wins[window.getIndex()], spec);
    const auto r = plotArea().toFloat();
    const int W = std::max (2, (int) r.getWidth());
    curve.assign ((size_t) W, -200.0f);
    const double binHz = an->getSampleRate() / (double) (1 << order);
    static const float smoothOct[] = { 0.0f, 1.0f / 6.0f, 1.0f / 12.0f, 1.0f / 24.0f, 1.0f / 48.0f };
    const float so = smoothOct[smooth.getIndex()];
    const float tiltDb = 1.5f * (float) tilt.getIndex();
    for (int x = 0; x < W; ++x)
    {
        const float f = xToHz (r.getX() + (float) x, r), f1 = xToHz (r.getX() + (float) x + 1.0f, r);
        float lo = f, hi = f1;
        if (so > 0.0f) { lo = std::min (lo, f * std::pow (2.0f, -so * 0.5f)); hi = std::max (hi, f * std::pow (2.0f, so * 0.5f)); }
        int b0 = (int) std::floor (lo / binHz), b1 = (int) std::ceil (hi / binHz);
        b0 = juce::jlimit (1, (int) spec.size() - 1, b0); b1 = juce::jlimit (b0, (int) spec.size() - 1, b1);
        float v = -200.0f;
        if (b1 - b0 <= 1)
        {
            // zwischen Bins interpolieren (glatte Kurve im Bass)
            const double fb = f / binHz;
            const int k = juce::jlimit (1, (int) spec.size() - 2, (int) fb);
            const float t = (float) (fb - k);
            v = spec[(size_t) k] + t * (spec[(size_t) k + 1] - spec[(size_t) k]);
        }
        else
            for (int k = b0; k <= b1; ++k) v = std::max (v, spec[(size_t) k]);
        curve[(size_t) x] = v + tiltDb * std::log2 (std::max (10.0f, f) / 1000.0f);
    }
}

void SpectrumView::pushSpectrogramColumn()
{
    const auto r = plotArea();
    if (r.getWidth() < 4 || r.getHeight() < 4) return;
    const bool vertical = orient.getIndex() == 1;
    if (gram.isNull() || gram.getWidth() != r.getWidth() || gram.getHeight() != r.getHeight())
    {
        gram = juce::Image (juce::Image::RGB, r.getWidth(), r.getHeight(), true);
        juce::Graphics gg (gram);
        gg.fillAll (paletteColour (palette.getIndex(), 0.0f));
    }
    const auto rf = r.toFloat().withPosition (0.0f, 0.0f);
    const float rng = range.getIndex() == 0 ? 120.0f : range.getIndex() == 1 ? 90.0f : range.getIndex() == 2 ? 60.0f : 30.0f;
    auto colourAt = [&] (float hz)
    {
        const float x = hzToX (hz, plotArea().toFloat()) - (float) plotArea().getX();
        const int xi = juce::jlimit (0, (int) curveMain.size() - 1, (int) x);
        const float t = juce::jlimit (0.0f, 1.0f, 1.0f + curveMain[(size_t) xi] / rng);
        return paletteColour (palette.getIndex(), std::pow (t, 1.6f));
    };
    if (! vertical)
    {
        gram.moveImageSection (0, 0, 1, 0, gram.getWidth() - 1, gram.getHeight());
        const int x = gram.getWidth() - 1;
        for (int y = 0; y < gram.getHeight(); ++y)
        {
            // Frequenz von unten (10 Hz) nach oben (22 kHz), logarithmisch
            const float hz = xToHz ((float) (gram.getHeight() - 1 - y) / (float) gram.getHeight() * rf.getWidth(), rf);
            gram.setPixelAt (x, y, colourAt (hz));
        }
    }
    else
    {
        gram.moveImageSection (0, 1, 0, 0, gram.getWidth(), gram.getHeight() - 1);
        for (int x = 0; x < gram.getWidth(); ++x)
            gram.setPixelAt (x, 0, colourAt (xToHz ((float) x, rf)));
    }
}

void SpectrumView::refresh()
{
    if (an == nullptr || an->totalSamples() < 1024) return;
    const int src = source.getSelected();
    computeCurve (src == 0 ? -1 : src == 2 ? LiveAnalyzer::Kick : src == 3 ? LiveAnalyzer::Bass : -1, curveMain);
    if (src == 1)
    {
        computeCurve (LiveAnalyzer::Kick, curveKick);
        computeCurve (LiveAnalyzer::Bass, curveBass);
    }
    if (scButton.getToggleState()) computeCurve (-2, curveSc);

    // Abklingen (Release) und Spitzen-Halten
    auto decay = [] (std::vector<float>& d, const std::vector<float>& c)
    {
        if (d.size() != c.size()) d = c;
        for (size_t i = 0; i < c.size(); ++i) d[i] = std::max (c[i], d[i] - 2.5f);
    };
    decay (disp, curveMain);
    if (src == 1) { decay (dispKick, curveKick); decay (dispBass, curveBass); }
    if (hold.size() != disp.size()) hold.assign (disp.size(), -200.0f);
    if (holdButton.getToggleState()) for (size_t i = 0; i < disp.size(); ++i) hold[i] = std::max (hold[i], disp[i]);
    if (mode.getSelected() == 2) pushSpectrogramColumn();
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    const auto r = plotArea().toFloat();
    plotPanel (g, r);
    const bool gramMode = mode.getSelected() == 2;

    if (gramMode && gram.isValid())
        g.drawImageAt (gram, plotArea().getX(), plotArea().getY());

    // Frequenzraster und musikalisches Raster
    g.setFont (uiFont (10.0f));
    const bool vertical = orient.getIndex() == 1;
    auto freqPos = [&] (float hz)   // Position entlang der Frequenzachse (x, im horizontalen Spektrogramm y)
    {
        if (gramMode && ! vertical) return r.getBottom() - (hzToX (hz, r) - r.getX()) / r.getWidth() * r.getHeight();
        return hzToX (hz, r);
    };
    const bool freqIsY = gramMode && ! vertical;
    for (float hz : { 20.0f, 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f })
    {
        const float p = freqPos (hz);
        g.setColour (cGrid);
        if (freqIsY) g.drawHorizontalLine ((int) p, r.getX(), r.getRight()); else g.drawVerticalLine ((int) p, r.getY(), r.getBottom());
        g.setColour (col::textDim);
        const juce::String lab = hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz);
        if (freqIsY) g.drawText (lab, juce::Rectangle<float> (r.getX() + 2.0f, p - 12.0f, 30.0f, 11.0f), juce::Justification::left);
        else g.drawText (lab, juce::Rectangle<float> (p - 16.0f, r.getBottom() - 13.0f, 32.0f, 12.0f), juce::Justification::centred);
    }
    if (gridButton.getToggleState())
        for (int oct = -1; oct <= 9; ++oct)
        {
            const int midi = 24 + 12 * oct + rootNote;   // Ableton: C0 = 24
            const float hz = (float) (440.0 * std::pow (2.0, (midi - 69) / 12.0));
            if (hz < 10.0f || hz > 22000.0f) continue;
            const float p = freqPos (hz);
            g.setColour (col::magenta.withAlpha (0.18f));
            if (freqIsY) g.drawHorizontalLine ((int) p, r.getX(), r.getRight()); else g.drawVerticalLine ((int) p, r.getY(), r.getBottom());
            g.setColour (col::magenta.withAlpha (0.8f));
            if (freqIsY) g.drawText (noteName (midi), juce::Rectangle<float> (r.getRight() - 34.0f, p - 12.0f, 32.0f, 11.0f), juce::Justification::right);
            else g.drawText (noteName (midi), juce::Rectangle<float> (p - 16.0f, r.getY() + 2.0f, 32.0f, 11.0f), juce::Justification::centred);
        }

    if (! gramMode)
    {
        // dB-Raster
        const float rng = range.getIndex() == 0 ? 120.0f : range.getIndex() == 1 ? 90.0f : range.getIndex() == 2 ? 60.0f : 30.0f;
        const float stepDb = rng > 60 ? 12.0f : rng > 30 ? 6.0f : 3.0f;
        for (float db = 0.0f; db >= -rng; db -= stepDb)
        {
            const float y = dbToY (db, r);
            g.setColour (cGrid);
            g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
            g.setColour (col::textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (r.getX() + 3.0f, y + 1.0f, 30.0f, 11.0f), juce::Justification::left);
        }

        auto drawCurve = [&] (const std::vector<float>& d, juce::Colour c, bool fill)
        {
            if (d.size() < 2) return;
            juce::Path p;
            for (size_t x = 0; x < d.size(); ++x)
            {
                const float px = r.getX() + (float) x, py = dbToY (d[x], r);
                if (x == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
            }
            if (fill)
            {
                juce::Path f (p);
                f.lineTo (r.getRight(), r.getBottom()); f.lineTo (r.getX(), r.getBottom()); f.closeSubPath();
                g.setGradientFill (juce::ColourGradient (c.withAlpha (0.35f), 0.0f, r.getY(), c.withAlpha (0.02f), 0.0f, r.getBottom(), false));
                g.fillPath (f);
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.3f));
        };
        auto drawBars = [&] (const std::vector<float>& d, juce::Colour c)
        {
            // 1/6-Oktav-Bänder ab 16 Hz
            for (float f0 = 16.0f; f0 < 20000.0f; f0 *= std::pow (2.0f, 1.0f / 6.0f))
            {
                const float f1 = f0 * std::pow (2.0f, 1.0f / 6.0f);
                const int x0 = (int) (hzToX (f0, r) - r.getX()), x1 = (int) (hzToX (f1, r) - r.getX());
                float v = -200.0f;
                for (int x = std::max (0, x0); x < std::min ((int) d.size(), x1); ++x) v = std::max (v, d[(size_t) x]);
                const auto bar = juce::Rectangle<float> (r.getX() + (float) x0 + 1.0f, dbToY (v, r), (float) (x1 - x0) - 2.0f, r.getBottom() - dbToY (v, r));
                g.setGradientFill (juce::ColourGradient (c, 0.0f, r.getY(), c.withAlpha (0.25f), 0.0f, r.getBottom(), false));
                g.fillRect (bar);
            }
        };
        if (scButton.getToggleState() && an != nullptr && an->levels().scPeak > 1.0e-5f) drawCurve (curveSc, cSc.withAlpha (0.8f), false);
        if (holdButton.getToggleState()) drawCurve (hold, col::amber.withAlpha (0.6f), false);
        const int src = source.getSelected();
        if (src == 1)
        {
            if (mode.getSelected() == 1) { drawBars (dispKick, cKick.withAlpha (0.6f)); drawBars (dispBass, cBass.withAlpha (0.5f)); }
            else { drawCurve (dispKick, cKick, true); drawCurve (dispBass, cBass, true); }
            drawCurve (disp, cOut.withAlpha (0.7f), false);
        }
        else if (mode.getSelected() == 1) drawBars (disp, src == 2 ? cKick : src == 3 ? cBass : col::ice);
        else drawCurve (disp, src == 2 ? cKick : src == 3 ? cBass : col::ice, true);
    }

    // Zeiger: Frequenz, Note, Pegel
    if (r.contains (hover))
    {
        const float hz = freqIsY ? xToHz (r.getX() + (r.getBottom() - hover.y) / r.getHeight() * r.getWidth(), r) : xToHz (hover.x, r);
        juce::String s = juce::String (hz < 1000.0f ? juce::String (hz, 1) + " Hz" : juce::String (hz / 1000.0f, 2) + " kHz") + "  " + hzToNote (hz);
        if (! gramMode && ! disp.empty())
        {
            const int xi = juce::jlimit (0, (int) disp.size() - 1, (int) (hover.x - r.getX()));
            s << "  " << juce::String (disp[(size_t) xi], 1) << " dB";
        }
        g.setColour (col::cyan.withAlpha (0.35f));
        if (freqIsY) g.drawHorizontalLine ((int) hover.y, r.getX(), r.getRight()); else g.drawVerticalLine ((int) hover.x, r.getY(), r.getBottom());
        g.setFont (uiFont (11.5f, true));
        const auto box = juce::Rectangle<float> (juce::jlimit (r.getX(), r.getRight() - 210.0f, hover.x + 8.0f), r.getY() + 16.0f, 210.0f, 18.0f);
        g.setColour (juce::Colour (0xd0020809));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (col::text);
        g.drawText (s, box, juce::Justification::centred);
    }
}

//==============================================================================
GonioView::GonioView (const LiveAnalyzer* a, ViewOptions& o)
    : an (a), opts (o),
      mode ({ "LISSAJOUS", "PUNKTE", "POLAR" }, col::cyan),
      decay ("NACHLEUCHTEN", { "schnell", "normal", "langsam" }, opts.get ("gn_decay", 1), col::cyan)
{
    mode.setSelected (opts.get ("gn_mode", 0));
    mode.onChange = [this] (int v) { opts.set ("gn_mode", v); phosphor = {}; };
    decay.onChange = [this] (int v) { opts.set ("gn_decay", v); };
    addAndMakeVisible (mode);
    addAndMakeVisible (decay);
}

void GonioView::resized()
{
    auto r = getLocalBounds().removeFromTop (22);
    mode.setBounds (r.removeFromLeft (mode.idealWidth() + 12));
    r.removeFromLeft (6);
    decay.setBounds (r.removeFromLeft (decay.idealWidth()));
    phosphor = {};
}

juce::Rectangle<int> GonioView::scopeArea() const
{
    auto r = getLocalBounds().withTrimmedTop (28).withTrimmedBottom (66);
    const int s = std::min (r.getWidth(), r.getHeight());
    return r.withSizeKeepingCentre (s, s);
}

void GonioView::refresh()
{
    if (an == nullptr) return;
    const auto sa = scopeArea();
    if (phosphor.isNull() || phosphor.getWidth() != sa.getWidth())
        phosphor = juce::Image (juce::Image::ARGB, std::max (1, sa.getWidth()), std::max (1, sa.getHeight()), true);
    const int64_t tot = an->totalSamples();
    const int fresh = (int) std::min<int64_t> (tot - lastTotal, 8192);
    lastTotal = tot;
    {
        juce::Graphics g (phosphor);
        // Nachleuchten: Bild abdunkeln
        const float keep = decay.getIndex() == 0 ? 0.55f : decay.getIndex() == 1 ? 0.75f : 0.88f;
        phosphor.multiplyAllAlphas (keep);
        if (fresh > 1)
        {
            float pk = 1.0e-4f;
            for (int i = 0; i < fresh; ++i) pk = std::max ({ pk, std::abs (an->hist (LiveAnalyzer::MixL, i)), std::abs (an->hist (LiveAnalyzer::MixR, i)) });
            const float sc = 0.7071f / pk * 0.92f;
            const float cx = (float) phosphor.getWidth() * 0.5f, cy = (float) phosphor.getHeight() * 0.5f, R = cx;
            auto pos = [&] (float l, float rr)
            {
                float x = (rr - l) * sc, y = (l + rr) * sc;   // Mitte senkrecht, Seite waagerecht
                if (mode.getSelected() == 2)
                {
                    // Polar: Winkel aus (S, M), auf die obere Halbebene gefaltet, Radius = Betrag
                    const float rad = std::sqrt (x * x + y * y);
                    float ang = std::atan2 (std::abs (y), x);
                    x = rad * std::cos (ang); y = rad * std::sin (ang);
                    return juce::Point<float> (cx + x * R, cy + R * 0.9f - y * R * 1.8f);
                }
                return juce::Point<float> (cx + x * R, cy - y * R);
            };
            if (mode.getSelected() == 0)
            {
                juce::Path p;
                for (int i = fresh - 1; i >= 0; --i)
                {
                    const auto q = pos (an->hist (LiveAnalyzer::MixL, i), an->hist (LiveAnalyzer::MixR, i));
                    if (i == fresh - 1) p.startNewSubPath (q); else p.lineTo (q);
                }
                g.setColour (col::cyan.withAlpha (0.8f));
                g.strokePath (p, juce::PathStrokeType (1.3f));
            }
            else
            {
                g.setColour (col::cyan.withAlpha (0.7f));
                for (int i = 0; i < fresh; i += std::max (1, fresh / 3000))
                {
                    const auto q = pos (an->hist (LiveAnalyzer::MixL, i), an->hist (LiveAnalyzer::MixR, i));
                    g.fillRect (q.x, q.y, 1.5f, 1.5f);
                }
            }
            // Sidechain als dünne Linie
            if (an->levels().scPeak > 1.0e-5f)
            {
                juce::Path p;
                for (int i = std::min (fresh, 1500) - 1; i >= 0; --i)
                {
                    const auto q = pos (an->hist (LiveAnalyzer::ScL, i), an->hist (LiveAnalyzer::ScR, i));
                    if (p.isEmpty()) p.startNewSubPath (q); else p.lineTo (q);
                }
                g.setColour (cSc.withAlpha (0.35f));
                g.strokePath (p, juce::PathStrokeType (0.6f));
            }
        }
    }
    flash = (flash + 1) % 30;
    repaint();
}

void GonioView::paint (juce::Graphics& g)
{
    const auto sa = scopeArea().toFloat();
    plotPanel (g, getLocalBounds().toFloat().withTrimmedTop (28));
    // Raster: Kreis, Achsen L / R / M / S
    g.setColour (cGrid);
    g.drawEllipse (sa.reduced (4.0f), 1.0f);
    g.drawLine (sa.getCentreX(), sa.getY(), sa.getCentreX(), sa.getBottom(), 1.0f);
    g.drawLine (sa.getX(), sa.getCentreY(), sa.getRight(), sa.getCentreY(), 1.0f);
    g.drawLine (sa.getX() + sa.getWidth() * 0.15f, sa.getY() + sa.getHeight() * 0.15f, sa.getRight() - sa.getWidth() * 0.15f, sa.getBottom() - sa.getHeight() * 0.15f, 0.6f);
    g.drawLine (sa.getRight() - sa.getWidth() * 0.15f, sa.getY() + sa.getHeight() * 0.15f, sa.getX() + sa.getWidth() * 0.15f, sa.getBottom() - sa.getHeight() * 0.15f, 0.6f);
    g.setFont (uiFont (10.5f, true));
    g.setColour (col::textDim);
    g.drawText ("M", sa.withHeight (14.0f).translated (0.0f, 2.0f), juce::Justification::centredTop);
    g.drawText ("L", juce::Rectangle<float> (sa.getX() + sa.getWidth() * 0.13f - 14.0f, sa.getY() + sa.getHeight() * 0.13f - 14.0f, 14.0f, 14.0f), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (sa.getRight() - sa.getWidth() * 0.13f, sa.getY() + sa.getHeight() * 0.13f - 14.0f, 14.0f, 14.0f), juce::Justification::centred);
    g.drawText ("+S", juce::Rectangle<float> (sa.getRight() - 22.0f, sa.getCentreY() - 14.0f, 20.0f, 12.0f), juce::Justification::centred);
    if (phosphor.isValid()) g.drawImageAt (phosphor, (int) sa.getX(), (int) sa.getY());
    if (an == nullptr) return;

    // Korrelation, Breite, Balance
    const auto& lv = an->levels();
    auto b = getLocalBounds().withTrimmedBottom (4).removeFromBottom (60).reduced (10, 0).toFloat();
    auto bar = [&] (juce::Rectangle<float> rr, float v, const juce::String& left, const juce::String& right, juce::Colour c)
    {
        g.setColour (juce::Colour (0xff0b1a1c));
        g.fillRoundedRectangle (rr, 3.0f);
        const float cx = rr.getCentreX(), x = rr.getX() + rr.getWidth() * (v * 0.5f + 0.5f);
        g.setColour (c);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::min (cx, x), rr.getY() + 2.0f, std::max (cx, x), rr.getBottom() - 2.0f));
        g.setColour (col::text.withAlpha (0.5f));
        g.drawVerticalLine ((int) cx, rr.getY(), rr.getBottom());
        g.setFont (uiFont (10.0f, true));
        g.setColour (col::textDim);
        g.drawText (left, rr.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        g.drawText (right, rr.reduced (4.0f, 0.0f), juce::Justification::centredRight);
    };
    auto row1 = b.removeFromTop (16.0f);
    bar (row1, lv.correlation, "-1", "+1", lv.correlation < -0.3f ? col::red : lv.correlation < 0.2f ? col::amber : col::green);
    b.removeFromTop (3.0f);
    auto row2 = b.removeFromTop (12.0f);
    bar (row2, lv.balance, "L", "R", col::ice);
    g.setFont (uiFont (12.0f, true));
    g.setColour (col::text);
    auto txt = b.withTrimmedTop (4.0f);
    g.drawText ("Corr " + juce::String (lv.correlation, 2) + T ("   ·   Width ") + juce::String (juce::roundToInt (lv.widthPct)) + " %"
                    + T ("   ·   Balance ") + juce::String (juce::roundToInt (lv.balance * 100.0f)) + " %",
                txt, juce::Justification::centred);
    if (lv.correlation < -0.3f && flash < 18)
    {
        g.setColour (col::red);
        g.setFont (uiFont (16.0f, true));
        g.drawText ("MONO!", sa.withTrimmedTop (sa.getHeight() * 0.75f), juce::Justification::centred);
    }
}

//==============================================================================
MeterStrip::MeterStrip (SamsiBasslineProcessor& p, LiveAnalyzer* a, ViewOptions& o) : proc (p), an (a), opts (o) {}

void MeterStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (an != nullptr) an->resetMeters(); repaint(); return; }
    const auto p = e.getPosition();
    if (lufsBox.contains (p)) opts.set ("mt_lufs", (opts.get ("mt_lufs", 0) + 1) % 3);
    else if (rmsBox.contains (p)) opts.set ("mt_rms", 1 - opts.get ("mt_rms", 0));
    else if (peakBox.contains (p)) opts.set ("mt_tp", 1 - opts.get ("mt_tp", 0));
    repaint();
}

void MeterStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    if (an == nullptr) return;
    const auto& lv = an->levels();
    auto info = r.removeFromBottom (128);
    auto scale = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 56.0f) / 65.0f); };
    auto bars = r.reduced (6, 6);
    auto ticks = bars.removeFromLeft (26);
    const int bw = (bars.getWidth() - 10) / 2;
    const juce::Rectangle<int> lr[2] = { bars.removeFromLeft (bw), bars.withTrimmedLeft (10).withWidth (bw) };
    g.setFont (uiFont (9.5f));
    for (int db : { 6, 0, -6, -12, -18, -24, -36, -48 })
    {
        const float y = (float) lr[0].getBottom() - (float) lr[0].getHeight() * scale ((float) db);
        g.setColour (col::textDim);
        g.drawText (juce::String (db), juce::Rectangle<float> ((float) ticks.getX(), y - 6.0f, 22.0f, 12.0f), juce::Justification::centredRight);
        g.setColour (cGrid);
        g.drawHorizontalLine ((int) y, (float) lr[0].getX(), (float) lr[1].getRight());
    }
    const float rmsV[2] = { lv.rmsL, lv.rmsR }, pkV[2] = { lv.peakL, lv.peakR };
    for (int c = 0; c < 2; ++c)
    {
        auto b = lr[c].toFloat();
        g.setColour (juce::Colour (0xff0b1a1c));
        g.fillRoundedRectangle (b, 3.0f);
        const float rmsDb = juce::Decibels::gainToDecibels (rmsV[c], -80.0f), pkDb = juce::Decibels::gainToDecibels (pkV[c], -80.0f);
        const auto fill = b.withTrimmedTop (b.getHeight() * (1.0f - scale (rmsDb)));
        juce::ColourGradient grad (col::blue, 0.0f, b.getBottom(), col::red, 0.0f, b.getY(), false);
        grad.addColour (0.6, col::turq);
        grad.addColour (0.86, col::amber);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (fill, 3.0f);
        const float py = b.getBottom() - b.getHeight() * scale (pkDb);
        g.setColour (pkDb > -0.1f ? col::red : col::text);
        g.fillRect (b.getX(), py - 1.0f, b.getWidth(), 2.0f);
        g.setColour (col::textDim);
        g.setFont (uiFont (10.5f, true));
        g.drawText (c == 0 ? "L" : "R", b.withY (b.getBottom() - 14.0f).withHeight (14.0f), juce::Justification::centred);
    }

    // Zahlen
    auto line = [&] (juce::Rectangle<int>& area, const juce::String& name, const juce::String& val, juce::Colour c, juce::Rectangle<int>* store)
    {
        auto row = area.removeFromTop (30);
        if (store != nullptr) *store = row;
        g.setColour (juce::Colour (0x60031014));
        g.fillRoundedRectangle (row.toFloat().reduced (2.0f, 1.0f), 4.0f);
        g.setFont (uiFont (9.5f, true));
        g.setColour (col::textDim);
        g.drawText (name, row.removeFromTop (12).reduced (6, 0), juce::Justification::centredLeft);
        g.setFont (uiFont (13.5f, true));
        g.setColour (c);
        g.drawText (val, row.reduced (6, 0), juce::Justification::centredLeft);
    };
    const bool tp = opts.get ("mt_tp", 0) != 0;
    const float pkDb = tp ? lv.truePeakDb : lv.samplePeakDb;
    line (info, tp ? "TRUE PEAK (dBTP)" : "SAMPLE PEAK (dBFS)", dbText (pkDb), pkDb > -0.1f ? col::red : col::text, &peakBox);
    const int lm = opts.get ("mt_lufs", 0);
    const double lufs = lm == 0 ? lv.lufsS : lm == 1 ? lv.lufsM : lv.lufsI;
    line (info, lm == 0 ? "LUFS-S (3 s)" : lm == 1 ? "LUFS-M (400 ms)" : "LUFS-I", dbText ((float) lufs), col::ice, &lufsBox);
    const bool crest = opts.get ("mt_rms", 0) != 0;
    line (info, crest ? "CREST (dB)" : "RMS (300 ms)", crest ? juce::String (lv.crestDb, 1) : dbText (lv.rmsDb), col::violet.brighter (0.4f), &rmsBox);
    const float gr = proc.getLimiterGainReductionDb();
    g.setFont (uiFont (10.0f, true));
    g.setColour (gr < -0.05f ? col::amber : col::textDim);
    g.drawText ("LIM GR " + juce::String (gr, 1) + " dB", info.removeFromTop (14), juce::Justification::centred);
    g.setColour (proc.isSidechainConnected() ? (lv.scPeak >= 1.0f ? col::red : cSc) : col::textDim.withAlpha (0.5f));
    g.drawText (proc.isSidechainConnected() ? "SC " + dbText (juce::Decibels::gainToDecibels (lv.scPeak, -120.0f)) + " dB" : T ("SC –"),
                info.removeFromTop (14), juce::Justification::centred);
}

//==============================================================================
ListenSwitch::ListenSwitch (juce::AudioProcessorValueTreeState& apvts)
    : attach (*apvts.getParameter (pid::uListen), [this] (float v) { value = juce::roundToInt (v); repaint(); }, nullptr)
{
    attach.sendInitialUpdate();
    setTooltip (T ("Abhören: L, R, ST (Stereo), M (Mitte), S (Seite). Umschalt-Klick auf L oder R = Dual-Mono (der Kanal auf beiden Seiten)."));
}

void ListenSwitch::paint (juce::Graphics& g)
{
    static const char* names[5] = { "L", "R", "ST", "M", "S" };
    static const int vals[5] = { 1, 2, 0, 3, 4 };
    const float w = (float) getWidth() / 5.0f;
    for (int i = 0; i < 5; ++i)
    {
        auto c = juce::Rectangle<float> ((float) i * w, 0.0f, w, (float) getHeight()).reduced (1.5f);
        const bool on = value == vals[i] || (vals[i] == 1 && value == 5) || (vals[i] == 2 && value == 6);
        g.setColour (on ? (i == 2 ? col::amber : col::cyan).withAlpha (0.85f) : juce::Colour (0xff0b1a1c));
        g.fillRoundedRectangle (c, 4.0f);
        g.setColour (on ? col::bgDeep : col::textDim);
        g.setFont (uiFont (12.0f, true));
        g.drawText (juce::String (names[i]) + ((value == 5 && i == 0) || (value == 6 && i == 1) ? "+" : ""), c, juce::Justification::centred);
    }
}

void ListenSwitch::mouseDown (const juce::MouseEvent& e)
{
    static const int vals[5] = { 1, 2, 0, 3, 4 };
    const int i = juce::jlimit (0, 4, (int) (e.position.x / ((float) getWidth() / 5.0f)));
    int v = vals[i];
    if (e.mods.isShiftDown() && (v == 1 || v == 2)) v = v == 1 ? 5 : 6;
    attach.setValueAsCompleteGesture ((float) v);
}

//==============================================================================
UtilityBar::UtilityBar (SamsiBasslineProcessor& p, LiveAnalyzer* a)
    : PanelFrame ("UTILITY · OUTPUT (MIX, KICK, BASS gleich)", col::cyan), proc (p), an (a),
      on (p.apvts, pid::uOn, "On"), post (p.apvts, pid::uPost, "Post"),
      invL (p.apvts, pid::uInvL, "Invert L"), invR (p.apvts, pid::uInvR, "Invert R"),
      link (p.apvts, pid::uLink, "Link"), swap (p.apvts, pid::uSwap, "Swap L/R"),
      dc (p.apvts, pid::uDc, "DC"), monoOn (p.apvts, pid::uMonoOn, "F"),
      phaseOn (p.apvts, pid::uPhaseOn, "P"), tsOn (p.apvts, pid::uTsOn, "T"), limOn (p.apvts, pid::uLimOn, "LIM"),
      listen (p.apvts),
      monoHz (p.apvts, pid::uMonoHz, "Mono bis", col::cyan),
      imager (p.apvts, pid::uImager, "Imager", col::cyan),
      tilt (p.apvts, pid::uTilt, "L/R Tilt", col::cyan),
      phase (p.apvts, pid::uPhase, "Phase", col::amber),
      ts (p.apvts, pid::uTs, "Time Shift", col::amber),
      mix (p.apvts, pid::uMix, "Mix", col::cyan),
      outL (p.apvts, pid::uOutL, "Output L", col::turq),
      outR (p.apvts, pid::uOutR, "Output R", col::violet.brighter (0.3f)),
      ceil (p.apvts, pid::uLimCeil, "Ceiling", col::magenta)
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &post, &invL, &invR, &link, &swap, &dc, &monoOn, &phaseOn, &tsOn, &limOn,
                                                             &listen, &monoHz, &imager, &tilt, &phase, &ts, &mix, &outL, &outR, &ceil,
                                                             &freeze, &resetMeters })
        addAndMakeVisible (c);
    on.get().setTooltip (T ("Utility ein/aus (aus = exakt unverändert)"));
    post.get().setTooltip (T ("Messung NACH der Utility (an) oder VOR ihr (aus)"));
    dc.get().setTooltip (T ("DC-Sperre (Hochpass 5 Hz)"));
    monoOn.get().setTooltip (T ("Mono-Filter: Seitensignal unterhalb der Frequenz entfernen (24 dB/Okt., nur Seite, keine Latenz)"));
    phaseOn.get().setTooltip (T ("Phasendrehung ein (Allpass-Paar 15 Hz–20 kHz). Auch bei 0° ist der Allpass aktiv – daher Schalter."));
    tsOn.get().setTooltip (T ("Time Shift ein: meldet 50 ms Latenz an den Host, dadurch auch negative Verschiebung (-50..+50 ms) möglich."));
    limOn.get().setTooltip (T ("Brickwall-Limiter NUR auf MIX (nichtlinear, 1,5 ms Vorausschau = Latenz). KICK/BASS werden gleich verzögert, aber nicht begrenzt."));
    link.get().setTooltip (T ("Output L und R gekoppelt (R folgt L)"));
    freeze.setClickingTogglesState (true);
    freeze.setTooltip (T ("Alle Anzeigen einfrieren (nicht gespeichert)"));
    freeze.onClick = [this] { if (an != nullptr) an->setFrozen (freeze.getToggleState()); };
    resetMeters.setTooltip (T ("Peaks, LUFS-I und Mittelwerte zurücksetzen"));
    resetMeters.onClick = [this] { if (an != nullptr) an->resetMeters(); };
    groupNames = { "", T ("KANÄLE · ABHÖREN"), "MONO", "BILD", T ("PHASE · ZEIT"), "AUSGANG", T ("LIMITER (nur MIX)"), "" };
}

void UtilityBar::refresh()
{
    const bool linked = proc.readSettings().utility.outLink;
    outR.setAlpha (linked ? 0.4f : 1.0f);
    outR.setEnabled (! linked);
    const float gr = proc.getLimiterGainReductionDb();
    repaint (grArea);
    juce::ignoreUnused (gr);
}

void UtilityBar::resized()
{
    auto r = content().withTrimmedTop (12);
    groups.clear();
    const int kw = 74, rowH = r.getHeight();
    auto take = [&] (int w) { auto g = r.removeFromLeft (w); r.removeFromLeft (10); groups.push_back (g); return g; };

    auto g0 = take (150);
    auto a0 = g0.removeFromTop (30).reduced (0, 2);
    on.setBounds (a0.removeFromLeft (72));
    a0.removeFromLeft (6);
    post.setBounds (a0);
    g0.removeFromTop (8);
    auto b0 = g0.removeFromTop (30).reduced (0, 2);
    freeze.setBounds (b0.removeFromLeft (72));
    b0.removeFromLeft (6);
    resetMeters.setBounds (b0);

    auto g1 = take (330);
    auto top = g1.removeFromTop (30).reduced (0, 2);
    invL.setBounds (top.removeFromLeft (92));
    top.removeFromLeft (4);
    listen.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (4);
    invR.setBounds (top);
    g1.removeFromTop (8);
    auto bot = g1.removeFromTop (30).reduced (0, 2);
    swap.setBounds (bot.removeFromLeft (110));
    bot.removeFromLeft (8);
    link.setBounds (bot.removeFromLeft (80));
    bot.removeFromLeft (8);
    dc.setBounds (bot.removeFromLeft (70));

    auto g2 = take (kw + 40);
    monoOn.setBounds (g2.removeFromLeft (38).withSizeKeepingCentre (38, 26));
    monoHz.setBounds (g2);

    auto g3 = take (kw * 2);
    imager.setBounds (g3.removeFromLeft (kw));
    tilt.setBounds (g3);

    auto g4 = take (kw * 2 + 80);
    phaseOn.setBounds (g4.removeFromLeft (38).withSizeKeepingCentre (38, 26));
    phase.setBounds (g4.removeFromLeft (kw));
    tsOn.setBounds (g4.removeFromLeft (38).withSizeKeepingCentre (38, 26));
    ts.setBounds (g4);

    auto g5 = take (kw * 3);
    mix.setBounds (g5.removeFromLeft (kw));
    outL.setBounds (g5.removeFromLeft (kw));
    outR.setBounds (g5);

    auto g6 = take (kw + 120);
    limOn.setBounds (g6.removeFromLeft (64).withSizeKeepingCentre (64, 26));
    ceil.setBounds (g6.removeFromLeft (kw));
    grArea = g6;
    juce::ignoreUnused (rowH);
}

void UtilityBar::paint (juce::Graphics& g)
{
    PanelFrame::paint (g);
    for (size_t i = 0; i < groups.size() && i < groupNames.size(); ++i)
    {
        if (groupNames[i].isEmpty()) continue;
        auto gr = groups[i].expanded (4, 0).withTop (groups[i].getY() - 14);
        g.setColour (col::cyan.withAlpha (0.06f));
        g.fillRoundedRectangle (gr.toFloat(), 6.0f);
        drawSectionLabel (g, gr.removeFromTop (12).translated (4, 0), groupNames[i], i == 6 ? col::magenta : col::cyan);
    }
    const float gr = proc.getLimiterGainReductionDb();
    g.setFont (uiFont (11.0f, true));
    g.setColour (gr < -0.05f ? col::amber : col::textDim);
    g.drawFittedText ("GR\n" + juce::String (gr, 1) + " dB", grArea, juce::Justification::centred, 2);
}

//==============================================================================
AnalyzerView::AnalyzerView (SamsiBasslineProcessor& p, LiveAnalyzer& a)
    : opts (p.apvts.state.getOrCreateChildWithName ("VIEWOPTIONS", nullptr)),
      scopeFrame ("SCOPE", col::cyan), kickFrame ("KICK · LIVE", col::turq), bassFrame ("BASS · LIVE", juce::Colour (0xffffd34a)),
      meterFrame ("PEGEL", col::cyan), specFrame ("SPEKTRUM", col::cyan), gonioFrame ("STEREO", col::cyan),
      scope (&a, opts), kickLive (LiveWaveView::Kind::Kick, &a, opts), bassLive (LiveWaveView::Kind::Bass, &a, opts),
      meters (p, &a, opts), spectrum (&a, opts), gonio (&a, opts), utility (p, &a),
      proc (p), an (a)
{
    for (auto* c : std::initializer_list<juce::Component*> { &scopeFrame, &kickFrame, &bassFrame, &meterFrame, &specFrame, &gonioFrame, &utility })
        addAndMakeVisible (c);
    scopeFrame.addAndMakeVisible (scope);
    kickFrame.addAndMakeVisible (kickLive);
    bassFrame.addAndMakeVisible (bassLive);
    kickLive.setProcessor (&p);
    bassLive.setProcessor (&p);
    meterFrame.addAndMakeVisible (meters);
    specFrame.addAndMakeVisible (spectrum);
    gonioFrame.addAndMakeVisible (gonio);
}

void AnalyzerView::resized()
{
    auto r = getLocalBounds();
    auto row1 = r.removeFromTop (300);
    r.removeFromTop (10);
    auto row2 = r.removeFromTop (390);
    r.removeFromTop (10);
    utility.setBounds (r);

    bassFrame.setBounds (row1.removeFromRight (310));
    row1.removeFromRight (10);
    kickFrame.setBounds (row1.removeFromRight (310));
    row1.removeFromRight (10);
    scopeFrame.setBounds (row1);

    meterFrame.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (10);
    gonioFrame.setBounds (row2.removeFromRight (420));
    row2.removeFromRight (10);
    specFrame.setBounds (row2);

    scope.setBounds (scopeFrame.content());
    kickLive.setBounds (kickFrame.content());
    bassLive.setBounds (bassFrame.content());
    meters.setBounds (meterFrame.content());
    spectrum.setBounds (specFrame.content());
    gonio.setBounds (gonioFrame.content());
}

void AnalyzerView::refresh()
{
    if (! isVisible()) return;
    if (an.isFrozen()) return;
    scope.refresh();
    kickLive.refresh();
    bassLive.refresh();
    meters.refresh();
    spectrum.refresh();
    gonio.refresh();
    utility.refresh();
}

} // namespace samsi::ui
