#include "PluginEditor.h"

#include "SamsiBinaryData.h"
#include "content/Presets.h"

using namespace samsi;
using namespace samsi::ui;

//==============================================================================
SamsaraEmblem::SamsaraEmblem()
{
    logo = juce::ImageCache::getFromMemory (SamsiBinary::samsara_logo_png, SamsiBinary::samsara_logo_pngSize);
    setInterceptsMouseClicks (false, false);
}

void SamsaraEmblem::advance (float kickLevel)
{
    rotation += 0.004f;
    if (rotation > juce::MathConstants<float>::twoPi) rotation -= juce::MathConstants<float>::twoPi;
    pulse = juce::jmax (pulse * 0.86f, juce::jlimit (0.0f, 1.0f, kickLevel * 1.4f));
    repaint();
}

void SamsaraEmblem::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    const float R = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;

    // Glühen (pulsiert mit der Kick)
    g.setGradientFill (juce::ColourGradient (col::cyan.withAlpha (0.18f + 0.35f * pulse), c.x, c.y,
                                             col::cyan.withAlpha (0.0f), c.x + R, c.y, true));
    g.fillEllipse (r);

    // Blütenkranz (dreht langsam)
    sym::petalRing (g, c, R * 0.86f, 12, R * 0.22f, rotation, col::cyan.withAlpha (0.55f + 0.45f * pulse), 0.3f + 0.7f * pulse);
    sym::dottedRing (g, c, R * 0.99f, 48, 1.6f, col::ice.withAlpha (0.6f));

    // Logo im Kreis
    const float lr = R * 0.66f;
    if (logo.isValid())
    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
        g.reduceClipRegion (clip);
        // Logo-Kreis füllt ca. 86 % des Bildes -> etwas vergrößern, damit der graue Rand verschwindet
        const float sz = lr * 2.0f / 0.84f;
        g.drawImage (logo, juce::Rectangle<float> (c.x - sz * 0.5f, c.y - sz * 0.5f, sz, sz));
    }
    juce::Path ring;
    ring.addEllipse (c.x - lr, c.y - lr, lr * 2.0f, lr * 2.0f);
    sym::glowStroke (g, ring, col::cyan, 1.4f, 0.6f + 0.4f * pulse);
}

//==============================================================================
SamsiBasslineEditor::Content::Content (SamsiBasslineProcessor& p, LiveAnalyzer& live)
    : proc (p), kick (p), bass (p), mix (p, MixPanel::Role::Scope), measure (p, MixPanel::Role::Measure),
      viewOpts (p.apvts.state.getOrCreateChildWithName ("VIEWOPTIONS", nullptr)),
      viewTabs ({ "SYNTH", T ("ANALYZER · UTILITY") }, col::cyan),
      analyzer (p, live),
      seq (p, p.apvts), fit (p, p.apvts),
      master (p.apvts, pid::master, "Master", col::cyan),
      busClip (p.apvts, pid::busClip, "Bus-Clipper"),
      busDrive (p.apvts, pid::busClipDrive, "Clip-Drive", col::magenta)
{
    for (auto* c : std::initializer_list<juce::Component*> { &emblem, &kick, &bass, &mix, &measure, &orb, &seq, &fit, &master, &busClip, &busDrive,
                                                             &presetBox, &prevButton, &nextButton, &saveButton, &loadButton })
        addAndMakeVisible (c);
    busClip.get().setTooltip (T ("Gemeinsamer, NICHTLINEARER Effekt nur auf dem MIX-Ausgang. "
                                 "Für den Nulltest MIX = KICK + BASS ausschalten."));

    orb.onClick = [this] { proc.startAutoFit(); };
    mix.attachLive (&live, viewOpts, &p);
    addAndMakeVisible (viewTabs);
    addChildComponent (analyzer);
    viewTabs.onChange = [this] (int v) { setView (v); };
    setView (viewOpts.get ("view", 0));

    refreshPresetBox();
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedId() - 1;
        if (idx >= 0) proc.applyFactoryPreset (idx);
    };
    prevButton.onClick = [this]
    {
        const int n = proc.getNumPresets();
        proc.applyFactoryPreset ((proc.getCurrentPreset() + n - 1) % n);
    };
    nextButton.onClick = [this] { proc.applyFactoryPreset ((proc.getCurrentPreset() + 1) % proc.getNumPresets()); };
    saveButton.setTooltip (T ("Eigenes Preset speichern (inkl. Pattern, Sample und Wavetable)"));
    loadButton.setTooltip (T ("Eigenes Preset laden"));
    saveButton.onClick = [this]
    {
        auto folder = SamsiBasslineProcessor::getUserPresetFolder();
        folder.createDirectory();
        chooser = std::make_unique<juce::FileChooser> ("Preset speichern", folder.getChildFile ("Mein Preset.samsipreset"), "*.samsipreset");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == juce::File()) return;
                                  proc.savePresetFile (f.withFileExtension ("samsipreset"));
                              });
    };
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Preset laden", SamsiBasslineProcessor::getUserPresetFolder(), "*.samsipreset");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile())
                                  {
                                      const auto err = proc.loadPresetFile (fc.getResult());
                                      presetHint = err.isEmpty() ? T ("Eigenes Preset: ") + fc.getResult().getFileNameWithoutExtension() : err;
                                      seq.refresh();
                                      repaint();
                                  }
                              });
    };
}

void SamsiBasslineEditor::Content::setView (int v)
{
    view = juce::jlimit (0, 1, v);
    viewOpts.set ("view", view);
    if (viewTabs.getSelected() != view) viewTabs.setSelected (view, false);
    for (auto* c : std::initializer_list<juce::Component*> { &kick, &bass, &mix, &measure, &orb, &seq, &fit })
        c->setVisible (view == 0);
    analyzer.setVisible (view == 1);
    repaint();
}

void SamsiBasslineEditor::Content::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& presets = factoryPresets();
    std::string lastTheme;
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (presets[i].theme != lastTheme)
        {
            lastTheme = presets[i].theme;
            if (i > 0) presetBox.addSeparator();
            presetBox.addSectionHeading (juce::String::fromUTF8 (lastTheme.c_str()));
        }
        presetBox.addItem (juce::String::fromUTF8 (presets[i].name.c_str()), (int) i + 1);
    }
    const int cur = proc.getCurrentPreset();
    presetBox.setSelectedId (cur + 1, juce::dontSendNotification);
    if (juce::isPositiveAndBelow (cur, (int) presets.size()))
        presetHint = juce::String::fromUTF8 (presets[(size_t) cur].hint.c_str());
}

void SamsiBasslineEditor::Content::resized()
{
    constexpr int m = 10, headerH = 84, sideW = 460;
    auto r = getLocalBounds().reduced (m);
    auto header = r.removeFromTop (headerH);
    r.removeFromTop (6);

    // links: Kick über Sequencer
    auto left = r.removeFromLeft (sideW);
    kick.setBounds (left.removeFromTop (466));
    left.removeFromTop (m);
    seq.setBounds (left);
    r.removeFromLeft (m);

    // rechts: Bass über Auto-Fit-Details
    auto right = r.removeFromRight (sideW);
    bass.setBounds (right.removeFromTop (466));
    right.removeFromTop (m);
    fit.setBounds (right);
    r.removeFromRight (m);

    // Mitte: Phase oben, Orb im Zentrum, Messung/Key unten
    mix.setBounds (r.removeFromTop (240));
    measure.setBounds (r.removeFromBottom (236));
    orb.setBounds (r.reduced (0, 2));

    // zweite Ansicht: Analyzer + Utility über die ganze Fläche unter dem Kopf
    analyzer.setBounds (getLocalBounds().reduced (m).withTrimmedTop (headerH + 6));

    emblem.setBounds (header.removeFromLeft (84).withSizeKeepingCentre (84, 84));
    header.removeFromLeft (10);
    titleArea = header.removeFromLeft (340);
    header.removeFromLeft (10);
    viewArea = header.removeFromLeft (250);
    viewTabs.setBounds (viewArea.withTrimmedTop (22).withHeight (40).reduced (0, 6));
    header.removeFromLeft (10);

    meterArea = header.removeFromRight (132).reduced (4, 6);
    header.removeFromRight (6);
    master.setBounds (header.removeFromRight (76));
    header.removeFromRight (10);
    busArea = header.removeFromRight (226).reduced (0, 2);
    auto ba = busArea.reduced (6, 0);
    busClip.setBounds (ba.removeFromLeft (130).withTrimmedTop (16).withSizeKeepingCentre (126, 26));
    busDrive.setBounds (ba.withTrimmedTop (2));
    header.removeFromRight (10);

    presetArea = header;
    auto top = presetArea.withTrimmedTop (18).removeFromTop (28);
    prevButton.setBounds (top.removeFromLeft (28));
    top.removeFromLeft (4);
    loadButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (4);
    saveButton.setBounds (top.removeFromRight (96));
    top.removeFromRight (4);
    nextButton.setBounds (top.removeFromRight (28));
    top.removeFromRight (4);
    presetBox.setBounds (top);
}

void SamsiBasslineEditor::Content::paint (juce::Graphics& g)
{
    if (background.isNull() || background.getWidth() != getWidth())
    {
        // Mandala-Hintergrund im Stil des Logos – einmal berechnet und zwischengespeichert
        background = juce::Image (juce::Image::ARGB, juce::jmax (1, getWidth()), juce::jmax (1, getHeight()), true);
        juce::Graphics bg (background);
        const auto b = getLocalBounds().toFloat();
        // Gunmetal-Vignette wie der Logo-Hintergrund
        bg.setGradientFill (juce::ColourGradient (col::bgMid, b.getCentreX(), b.getHeight() * 0.52f,
                                                  juce::Colour (0xff1c2a2c), 0.0f, 0.0f, true));
        bg.fillAll();
        bg.setGradientFill (juce::ColourGradient (juce::Colour (0xff010303), b.getCentreX(), b.getHeight() * 0.52f,
                                                  juce::Colour (0x00010303), b.getCentreX() + 820.0f, b.getHeight() * 0.52f, true));
        bg.fillRect (b);

        const auto c = orb.getBounds().toFloat().getTopLeft() + orb.orbCentre();
        // runder Gesamteindruck: dunkle Ecken, großer Chromring um das Zentrum
        {
            juce::ColourGradient corners (juce::Colour (0x00000000), c.x, c.y, juce::Colour (0xc0000000), c.x + b.getWidth() * 0.62f, c.y, true);
            corners.addColour (0.72, juce::Colour (0x00000000));
            bg.setGradientFill (corners);
            bg.fillRect (b);
            const float big = b.getHeight() * 0.5f - 6.0f;
            juce::Path ringP;
            ringP.addEllipse (c.x - big, c.y - big, big * 2.0f, big * 2.0f);
            bg.setColour (col::ice.withAlpha (0.10f));
            bg.strokePath (ringP, juce::PathStrokeType (2.0f));
            sym::dottedRing (bg, c, big - 10.0f, 120, 2.0f, col::cyan.withAlpha (0.10f));
        }
        for (int ring = 0; ring < 6; ++ring)
        {
            const float rad = 190.0f + (float) ring * 120.0f;
            juce::Path circle;
            circle.addEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            bg.setColour (col::cyan.withAlpha (0.07f));
            bg.strokePath (circle, juce::PathStrokeType (1.5f));
            sym::dottedRing (bg, c, rad + 16.0f, 36 + ring * 12, 2.2f, col::ice.withAlpha (0.08f));
            sym::petalRing (bg, c, rad + 50.0f, 12 + ring * 4, 26.0f, (float) ring * 0.13f, col::cyan.withAlpha (0.07f), 0.15f);
        }
        // feine psychedelische Strahlen in Türkis, Blau und Violett
        for (int i = 0; i < 144; ++i)
        {
            const float a = (float) i / 144.0f * juce::MathConstants<float>::twoPi;
            const float hue = 0.47f + 0.12f * std::sin (a * 3.0f);
            bg.setColour (juce::Colour::fromHSV (hue, 0.7f, 1.0f, 0.018f));
            bg.drawLine (c.x + std::cos (a) * 140.0f, c.y + std::sin (a) * 140.0f,
                         c.x + std::cos (a) * 1400.0f, c.y + std::sin (a) * 1100.0f, 1.0f);
        }
    }
    g.drawImageAt (background, 0, 0);

    // Titel in Chrom mit Glühen
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (uiFont (34.0f, true), "SAMSI BASSLINE", (float) titleArea.getX(), (float) titleArea.getY() + 44.0f);
        juce::Path text;
        ga.createPath (text);
        g.setColour (col::cyan.withAlpha (0.3f));
        g.strokePath (text, juce::PathStrokeType (5.0f));
        g.setGradientFill (chromeGradient (text.getBounds()));
        g.fillPath (text);
        g.setColour (col::cyan);
        g.strokePath (text, juce::PathStrokeType (0.7f));

        g.setFont (uiFont (12.5f, true));
        g.setColour (col::ice);
        g.drawText (T ("by SAMSARA  ·  PSY KICK & BASS  ·  PHASE FIT  ·  v" SAMSI_VERSION_STRING),
                    juce::Rectangle<int> (titleArea.getX() + 2, titleArea.getY() + 52, titleArea.getWidth(), 18), juce::Justification::centredLeft);
    }

    // Preset-Bereich
    g.setFont (uiFont (11.0f, true));
    g.setColour (col::cyan);
    g.drawText ("STYLE-PRESET", presetArea.withHeight (16), juce::Justification::centredLeft);
    g.setFont (uiFont (11.0f));
    g.setColour (col::textDim);
    g.drawFittedText (presetHint, presetArea.withTrimmedTop (50).withHeight (30), juce::Justification::topLeft, 2);

    // Bus-FX-Bereich (klar als nichtlinear markiert)
    g.setColour (col::magenta.withAlpha (0.12f));
    g.fillRoundedRectangle (busArea.toFloat(), 8.0f);
    g.setColour (col::magenta.withAlpha (0.7f));
    g.drawRoundedRectangle (busArea.toFloat(), 8.0f, 1.0f);
    g.setFont (uiFont (11.0f, true));
    g.drawText ("MIX-BUS FX (NICHTLINEAR)", busArea.withHeight (16).translated (8, 1), juce::Justification::centredLeft);

    // Pegelanzeigen MIX / KICK / BASS
    const juce::String names[3] = { "MIX", "KICK", "BASS" };
    const juce::Colour cols[3] = { col::sum, col::kick, col::bass.brighter (0.3f) };
    const int bw = meterArea.getWidth() / 3;
    for (int i = 0; i < 3; ++i)
    {
        auto cell = juce::Rectangle<int> (meterArea.getX() + i * bw, meterArea.getY(), bw, meterArea.getHeight()).reduced (5, 0);
        g.setColour (col::textDim);
        g.setFont (uiFont (10.0f, true));
        g.drawText (names[i], cell.removeFromBottom (12).expanded (4, 0), juce::Justification::centred);
        auto bar = cell.toFloat();
        g.setColour (juce::Colour (0xff0b1a1c));
        g.fillRoundedRectangle (bar, 3.0f);
        const float db = juce::Decibels::gainToDecibels (meter[i], -60.0f);
        const float norm = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 63.0f);
        g.setColour (db > -0.5f ? col::red : cols[i]);
        g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() * (1.0f - norm)), 3.0f);
    }
}

//==============================================================================
SamsiBasslineEditor::SamsiBasslineEditor (SamsiBasslineProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    live.prepare (p.getAnalysisSampleRate());
    p.liveTap.setEnabled (true);
    content = std::make_unique<Content> (p, live);
    addAndMakeVisible (*content);
    content->setBounds (0, 0, kBaseW, kBaseH);

    setResizable (true, true);
    setResizeLimits (kBaseW * 6 / 10, kBaseH * 6 / 10, kBaseW * 3 / 2, kBaseH * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
    setSize (kBaseW * 85 / 100, kBaseH * 85 / 100);
    startTimerHz (30);
}

SamsiBasslineEditor::~SamsiBasslineEditor()
{
    stopTimer();
    proc.liveTap.setEnabled (false);
    content.reset();
    setLookAndFeel (nullptr);
}

void SamsiBasslineEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bgDeep);
}

void SamsiBasslineEditor::resized()
{
    const float scale = (float) getWidth() / (float) kBaseW;
    content->setTransform (juce::AffineTransform::scale (scale));
}

void SamsiBasslineEditor::showPages (int kickPage, int bassPage, int mixPage, int fitPage)
{
    content->kick.showPage (kickPage);
    content->bass.showPage (bassPage);
    content->measure.showPage (mixPage);
    content->fit.showPage (fitPage);
}

void SamsiBasslineEditor::updateOrb()
{
    // Puls-Bedingung (nur echte Messwerte der aktuellen Vorschau):
    //  - Vorschau gehört zum aktuellen Klang (nicht veraltet), Fit ist aktiv (A/B = NACHHER)
    //  - schlimmste Auslöschung 20-60 Hz < 3 dB
    //  - Tonart-Prüfung ohne roten Befund
    FlowerOrb::State st;
    st.running = proc.analysis.isFitRunning();
    st.progress = proc.analysis.getFitProgress();
    st.fitOn = proc.readSettings().fit.enabled;
    st.keyOk = keyOk;
    const auto pv = proc.analysis.getPreview();
    st.known = pv != nullptr && pv->hash == requestedHash;
    if (st.known) st.worstCancelDb = (float) pv->metrics.cancelWorstDb[0];
    st.aligned = st.known && ! st.running && st.fitOn && st.keyOk && st.worstCancelDb < 3.0f;

    if (st.running)
    {
        st.line1 = T ("Auto Fit läuft: ") + juce::String (juce::roundToInt (st.progress * 100.0f)) + " %";
        st.line2 = T ("rendert Kandidaten im Hintergrund");
    }
    else if (! st.known)
    {
        st.line1 = T ("Messe …");
        st.line2 = {};
    }
    else
    {
        const auto worst = juce::String (st.worstCancelDb, 1) + " dB";
        if (st.aligned)        st.line1 = T ("PHASE IM ZIEL · Auslöschung max ") + worst;
        else if (! st.fitOn)   st.line1 = T ("A/B steht auf VORHER (Fit aus)");
        else if (! st.keyOk)   st.line1 = T ("Kick und Bass nicht in der Tonart · siehe KEY");
        else                   st.line1 = T ("Auslöschung max ") + worst + T (" · klicken für Auto Fit");
        const auto fitR = proc.getLastFit();
        const auto& f = proc.readSettings().fit;
        st.line2 = T ("Phase ") + juce::String (f.phaseDeg, 1) + T ("°  ·  Versatz ") + juce::String (f.offsetMs, 2)
                   + T (" ms  ·  Trim ") + juce::String (f.trimDb, 1) + " dB"
                   + (fitR != nullptr && proc.isFitStale() && ! st.aligned ? T ("  ·  Analyse veraltet") : juce::String());
    }
    content->orb.setState (st);
}

void SamsiBasslineEditor::showView (int v) { content->setView (v); }

samsi::ui::LiveWaveView* SamsiBasslineEditor::getLiveView (int which, int view)
{
    if (view == 0) return content->mix.getLiveView (which);
    return which == 0 ? &content->analyzer.kickLive : &content->analyzer.bassLive;
}

void SamsiBasslineEditor::timerCallback()
{
    ++ticks;

    // Live-Daten abholen (Audio-Thread -> Anzeige); bei geänderter Abtastrate neu vorbereiten
    if (std::abs (live.getSampleRate() - proc.getAnalysisSampleRate()) > 0.5)
        live.prepare (proc.getAnalysisSampleRate());
    live.drain (proc.liveTap);
    content->mix.refreshLive();
    content->analyzer.refresh();

    // Vorschau neu berechnen, wenn sich Klang/Pattern/Tempo/Fit-Werte/Inhalte geändert haben (max. ~6/s)
    if (ticks % 5 == 0)
    {
        const auto s = proc.readSettings();
        const auto pat = proc.pattern.snapshot();
        uint64_t h = soundHash (s, pat, proc.getAnalysisBpm(), proc.getAnalysisSampleRate(), proc.layerId());
        const auto& f = s.fit;
        for (float v : { f.enabled ? 1.0f : 0.0f, f.phaseDeg, f.offsetMs, f.duckDb, f.duckMs, f.trimDb, f.attackAddMs,
                         f.kickTailPct, f.polarity ? 1.0f : 0.0f, (float) f.phaseMode, s.global.masterDb,
                         s.global.busClipOn ? 1.0f : 0.0f, s.global.busClipDriveDb })
        {
            uint32_t bits;
            std::memcpy (&bits, &v, sizeof bits);
            h = (h ^ bits) * 1099511628211ull;
        }
        if (h != requestedHash)
        {
            requestedHash = h;
            proc.analysis.requestPreview (s, pat, proc.getAnalysisSampleRate(), proc.getAnalysisBpm(), h, proc.makeLayerRefs());
        }

        content->kick.update();
        content->bass.update();
        content->measure.updateKey();
        {
            const auto rep = proc.analyzeKey();
            keyOk = true;
            for (size_t i = 0; i < 3 && i < rep.checks.size(); ++i)
                keyOk = keyOk && rep.checks[i].status != Status::Bad;
        }
        content->seq.refresh();

        if (proc.getCurrentPreset() != shownProgram)
        {
            shownProgram = proc.getCurrentPreset();
            content->refreshPresetBox();
            content->repaint (content->getLocalBounds().removeFromTop (100));
        }
    }

    const auto pv = proc.analysis.getPreview();
    content->mix.setPreview (pv, pv == nullptr || pv->hash != requestedHash);
    content->measure.setPreview (pv, pv == nullptr || pv->hash != requestedHash);

    if (proc.pollFitResult())
        content->seq.refresh();
    content->fit.update();
    content->fit.refreshGlue();

    const bool running = proc.isHostPlaying() || proc.isGateOpen();
    content->seq.setPlayStep (running ? proc.getCurrentStep() : -1);

    bool meterChanged = false;
    for (int i = 0; i < 3; ++i)
    {
        const float pk = proc.takePeak (i);
        const float decayed = content->meter[i] * 0.8f;
        const float v = juce::jmax (pk, decayed < 1.0e-4f ? 0.0f : decayed);
        if (std::abs (v - content->meter[i]) > 1.0e-5f) { content->meter[i] = v; meterChanged = true; }
    }
    if (meterChanged)
        content->repaint (content->getWidth() - 150, 0, 150, 100);

    content->emblem.advance (content->meter[1]);
    updateOrb();
    content->orb.advance (content->meter[1]);
}
