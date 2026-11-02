#include "BassPanel.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../content/SampleLoader.h"

namespace samsi::ui
{

namespace
{
    const char* kRowTitles[4] = { "FILTER", "FILTER-HÜLLKURVE · GLIDE", "AMP-HÜLLKURVE", "STEREO · GROOVE" };
}

//==============================================================================
void WavePreview::setTable (std::shared_ptr<const WavetableData> t, float p, bool userEmpty)
{
    if (t == table && std::abs (p - pos) < 1.0e-4f && userEmpty == emptyUser) return;
    table = std::move (t);
    pos = p;
    emptyUser = userEmpty;
    repaint();
}

void WavePreview::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xc0020809));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::violet.withAlpha (0.4f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    if (table == nullptr || table->numFrames <= 0)
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (emptyUser ? T ("Eigene Tabelle: Datei hierher ziehen oder LADEN") : T ("–"),
                          getLocalBounds().reduced (6), juce::Justification::centred, 3);
        return;
    }

    // gestaffelte Frames (hinten -> vorne), aktueller Frame hell
    constexpr int layers = 9;
    const auto area = r.reduced (8.0f, 8.0f);
    const int L = 2;   // mittlere Mip-Stufe für die Darstellung reicht
    const int n = table->size[(size_t) L];
    for (int li = layers - 1; li >= 0; --li)
    {
        const float fpos = (float) li / (float) (layers - 1);
        const float dx = (float) li * 3.0f, dy = -(float) li * 2.5f;
        auto a = area.withTrimmedRight (layers * 3.0f).withTrimmedTop (layers * 2.5f).translated (dx, dy);
        juce::Path p;
        for (int i = 0; i <= 96; ++i)
        {
            const double ph = (double) i / 96.0;
            const float v = table->read (fpos, ph, L);
            const float x = a.getX() + a.getWidth() * (float) ph, y = a.getCentreY() - v * a.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        juce::ignoreUnused (n);
        g.setColour (col::violet.withAlpha (0.18f + 0.04f * (float) (layers - li)));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }
    {
        const int li = juce::roundToInt (pos * (layers - 1));
        auto a = area.withTrimmedRight (layers * 3.0f).withTrimmedTop (layers * 2.5f).translated ((float) li * 3.0f, -(float) li * 2.5f);
        juce::Path p;
        for (int i = 0; i <= 160; ++i)
        {
            const double ph = (double) i / 160.0;
            const float v = table->read (pos, ph, 0);
            const float x = a.getX() + a.getWidth() * (float) ph, y = a.getCentreY() - v * a.getHeight() * 0.45f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        sym::glowStroke (g, p, col::cyan, 1.6f, 0.8f);
    }
    g.setColour (col::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText (juce::String (table->numFrames) + " Frames", getLocalBounds().reduced (6, 3), juce::Justification::bottomLeft);
}

//==============================================================================
BassPanel::BassPanel (SamsiBasslineProcessor& p)
    : PanelFrame ("BASS", col::violet), proc (p),
      tabs (juce::StringArray { T ("OSZILLATOR"), T ("WAVETABLE"), T ("PICK · DÄMPFER"), T ("SAMPLE") }, col::violet.brighter (0.3f))
{
    auto& a = p.apvts;
    auto knob = [&a] (const char* id, const char* label, juce::Colour c = col::violet.brighter (0.25f))
    { return std::unique_ptr<juce::Component> (std::make_unique<Knob> (a, id, label, c)); };

    pages[0].push_back (knob (pid::bSaw, "Saw"));
    pages[0].push_back (knob (pid::bSquare, "Square"));
    pages[0].push_back (knob (pid::bPw, "Pulsbreite"));
    pages[0].push_back (knob (pid::bSub, "Sub"));
    pages[0].push_back (std::make_unique<Choice> (a, pid::bSubOct, "Sub-Okt."));
    pages[0].push_back (knob (pid::bPhase, "Startphase", col::turq));

    pages[1].push_back (knob (pid::wtLevel, "WT-Pegel", col::cyan));
    pages[1].push_back (std::make_unique<Choice> (a, pid::wtTable, "Tabelle"));
    pages[1].push_back (knob (pid::wtPos, "Morph", col::cyan));
    pages[1].push_back (knob (pid::wtEnv, "Morph-Env", col::magenta));

    pages[2].push_back (knob (pid::pickAmt, "Pick", col::amber));
    pages[2].push_back (knob (pid::pickPos, "Anschlag", col::amber));
    pages[2].push_back (knob (pid::damper, "Dämpfer", col::amber));

    pages[3].push_back (knob (pid::bsLevel, "Pegel", col::cyan));
    pages[3].push_back (knob (pid::bsTune, "Tune", col::cyan));
    pages[3].push_back (knob (pid::bsStart, "Start", col::cyan));
    pages[3].push_back (knob (pid::bsPitchEnv, "Pitch-Env", col::violet.brighter (0.3f)));
    smpThru = std::make_unique<Toggle> (a, pid::bsThru, T ("Filter+Drive"));
    smpThru->get().setTooltip (T ("An: das Sample läuft wie die Oszillatoren durch Pick, Dämpfer, Filter (mit Filter-Hüllkurve, Keytrack, Velocity) "
                                  "und Drive. Aus: nur Amp-Hüllkurve und Velocity. Pitch-Env: Filter-Hüllkurve verstimmt das Sample (±24 Halbtöne)."));

    rows[0].push_back (knob (pid::bCutoff, "Cutoff"));
    rows[0].push_back (knob (pid::bReso, "Resonanz"));
    rows[0].push_back (knob (pid::bEnvAmt, "Env-Menge"));
    rows[0].push_back (knob (pid::bKeytrack, "Keytrack"));
    rows[0].push_back (std::make_unique<Choice> (a, pid::bFilterMode, "Typ"));
    rows[0].push_back (knob (pid::bDrive, "Drive", col::magenta));

    rows[1].push_back (knob (pid::bFAtt, "F-Attack"));
    rows[1].push_back (knob (pid::bFDec, "F-Decay"));
    rows[1].push_back (knob (pid::bFSus, "F-Sustain"));
    rows[1].push_back (knob (pid::bFRel, "F-Release"));
    rows[1].push_back (knob (pid::bVelFilt, "Vel > Filter"));
    rows[1].push_back (knob (pid::bGlide, "Glide"));

    rows[2].push_back (knob (pid::bAtt, "Attack"));
    rows[2].push_back (knob (pid::bDec, "Decay"));
    rows[2].push_back (knob (pid::bSus, "Sustain"));
    rows[2].push_back (knob (pid::bRel, "Release"));
    rows[2].push_back (knob (pid::bVelAmp, "Vel > Amp"));
    rows[2].push_back (knob (pid::bLevel, "Pegel", col::turq));

    rows[3].push_back (knob (pid::bWidth, "Breite"));
    rows[3].push_back (knob (pid::bWidthHz, "Breite ab"));
    rows[3].push_back (knob (pid::bGroove, "Groove", col::amber));
    rows[3].push_back (std::make_unique<Toggle> (a, pid::bPhaseReset, "Phasenreset (Präzision)"));

    for (auto& pg : pages) for (auto& c : pg) addChildComponent (*c);
    for (auto& row : rows) for (auto& c : row) addAndMakeVisible (*c);
    addChildComponent (preview);
    for (auto* comp : std::initializer_list<juce::Component*> { &smpLoadButton, &smpClearButton, &smpStrip, smpThru.get() })
        addChildComponent (comp);
    smpLoadButton.setTooltip (T ("Eigenes Bass-Sample laden (tonal, z. B. eine gesampelte Bassnote). Der Grundton wird erkannt; "
                                 "das Sample wird pro Note gestimmt und mit derselben Startphase wie der Oszillator "
                                 "gestartet – Auto Fit stimmt es damit automatisch zur Kick ab."));
    smpLoadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Bass-Sample laden", juce::File(), kAudioFilePatterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadSample (fc.getResult()); });
    };
    smpClearButton.setTooltip ("Bass-Sample entfernen");
    smpClearButton.onClick = [this] { proc.clearBassSample(); smpMessage.clear(); update(); repaint(); };
    addChildComponent (loadButton);
    addChildComponent (clearButton);

    loadButton.setTooltip (T ("Eigene Wavetable laden: Serum/Vital-WAV (2048er-Frames), Einzelzyklus "
                              "oder beliebiges tonales Sample (wird tonhöhensynchron zerlegt)."));
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Wavetable laden", juce::File(), kAudioFilePatterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult()); });
    };
    clearButton.setTooltip ("Eigene Wavetable entfernen");
    clearButton.onClick = [this] { proc.clearWavetable(); message.clear(); update(); };

    addAndMakeVisible (tabs);
    tabs.onChange = [this] (int pg) { setPage (pg); };
    setPage (0);
}

void BassPanel::setPage (int page)
{
    for (int i = 0; i < 4; ++i)
        for (auto& c : pages[i]) c->setVisible (i == page);
    for (auto* comp : std::initializer_list<juce::Component*> { &smpLoadButton, &smpClearButton, &smpStrip, smpThru.get() })
        comp->setVisible (page == 3);
    preview.setVisible (page == 1);
    loadButton.setVisible (page == 1);
    clearButton.setVisible (page == 1);
    repaint();
}

void BassPanel::load (const juce::File& f)
{
    message = proc.loadWavetable (f);
    if (message.isEmpty())
    {
        if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (pid::wtLevel)))
            if (prm->getValue() < 0.01f) prm->setValueNotifyingHost (prm->convertTo0to1 (0.8f));
        message = proc.getWavetableInfo();
        tabs.setSelected (1);
    }
    update();
    repaint();
}

void BassPanel::loadSample (const juce::File& f)
{
    smpMessage = proc.loadBassSample (f);
    smpError = smpMessage.isNotEmpty();
    if (! smpError)
    {
        tabs.setSelected (3);
        if (auto s = proc.getBassSample())
            smpMessage = s->pitchHz > 0.0f ? T ("Grundton erkannt: ") + juce::String (s->pitchHz, 1) + " Hz ("
                                                 + juce::String (juce::roundToInt (s->pitchConfidence * 100.0f)) + T (" % sicher)")
                                           : T ("Kein klarer Grundton erkannt – Stimmung über Tune");
    }
    update();
    repaint();
}

void BassPanel::update()
{
    if (auto s = proc.getBassSample(); s != shownSample)
    {
        shownSample = s;
        smpStrip.setSample (s);
    }
    smpStrip.setMarkers (proc.readSettings().bass.smpStartMs, 2000.0f, 1.0f, proc.readSettings().bass.smpTuneSt);
    smpClearButton.setEnabled (shownSample != nullptr);
    const auto s = proc.readSettings().bass;
    preview.setTable (proc.getWavetableForSlot (s.wtTable), s.wtPos, s.wtTable == kUserTableSlot && proc.getUserWavetable() == nullptr);
    clearButton.setEnabled (proc.getUserWavetable() != nullptr);
}

bool BassPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void BassPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            // Ablegen auf der SAMPLE-Seite lädt ein Bass-Sample, sonst eine Wavetable
            if (tabs.getSelected() == 3) loadSample (juce::File (f)); else load (juce::File (f));
            break;
        }
}

void BassPanel::resized()
{
    auto r = content();
    tabs.setBounds (r.removeFromTop (22));
    const int rowH = (r.getHeight() - 4 * 12) / 5;
    auto first = r.removeFromTop (rowH);
    const int cw = first.getWidth() / 6;

    auto place = [rowH] (juce::Component& c, juce::Rectangle<int> cell)
    {
        if (dynamic_cast<Choice*> (&c) != nullptr)
            cell = cell.reduced (2, juce::jmax (0, (rowH - 46) / 2));
        c.setBounds (cell);
    };

    for (size_t j = 0; j < pages[0].size(); ++j)
        place (*pages[0][j], { first.getX() + (int) j * cw, first.getY(), cw, rowH });

    // Wavetable: Pegel, Tabelle (breit), Morph, Morph-Env, Vorschau
    place (*pages[1][0], { first.getX(), first.getY(), cw, rowH });
    place (*pages[1][1], { first.getX() + cw, first.getY(), cw + cw / 2, rowH });
    place (*pages[1][2], { first.getX() + cw * 5 / 2, first.getY(), cw, rowH });
    place (*pages[1][3], { first.getX() + cw * 7 / 2, first.getY(), cw, rowH });
    {
        auto pv = juce::Rectangle<int> (first.getX() + cw * 9 / 2, first.getY() + 2, first.getRight() - (first.getX() + cw * 9 / 2), rowH - 4);
        preview.setBounds (pv);
        loadButton.setBounds (pv.getRight() - 64, pv.getY() + 2, 44, 18);
        clearButton.setBounds (pv.getRight() - 20, pv.getY() + 2, 18, 18);
    }

    for (size_t j = 0; j < pages[2].size(); ++j)
        place (*pages[2][j], { first.getX() + (int) j * cw, first.getY(), cw, rowH });

    // Sample: Pegel, Tune, Start | Wellenform mit LADEN / X
    for (size_t j = 0; j < pages[3].size(); ++j)
        place (*pages[3][j], { first.getX() + (int) j * cw, first.getY(), cw, rowH });
    {
        auto sa = juce::Rectangle<int> (first.getX() + 4 * cw + 4, first.getY() + 2, first.getRight() - (first.getX() + 4 * cw + 4), rowH - 4);
        auto btn = sa.removeFromTop (20);
        smpLoadButton.setBounds (btn.removeFromLeft (52));
        btn.removeFromLeft (4);
        smpClearButton.setBounds (btn.removeFromLeft (20));
        btn.removeFromLeft (4);
        smpThru->setBounds (btn);
        sa.removeFromTop (2);
        smpInfo = sa.removeFromTop (12);
        smpStrip.setBounds (sa);
    }
    pickHelp = { first.getX() + 3 * cw + 4, first.getY(), cw * 3 - 4, rowH };

    for (int i = 0; i < 4; ++i)
    {
        labels[i] = r.removeFromTop (12);
        auto row = r.removeFromTop (rowH);
        for (size_t j = 0; j < rows[i].size(); ++j)
        {
            auto cell = juce::Rectangle<int> (row.getX() + (int) j * cw, row.getY(), cw, rowH);
            if (i == 3 && j == 3)   // Toggle über drei Spalten
                cell = juce::Rectangle<int> (row.getX() + 3 * cw + 6, row.getY(), cw * 3 - 6, rowH).withSizeKeepingCentre (cw * 3 - 12, 28);
            place (*rows[i][j], cell);
        }
    }
}

void BassPanel::paintOverChildren (juce::Graphics& g)
{
    for (int i = 0; i < 4; ++i)
        drawSectionLabel (g, labels[i], kRowTitles[i], i == 3 ? col::cyan : col::violet.brighter (0.45f));

    if (tabs.getSelected() == 2)
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (T ("Zupf-Modell: Anschlag setzt Kammfilter-Kerben (wie die Position am Saitenende), "
                             "Dämpfer lässt die Höhen nach dem Anschlag schneller abklingen."),
                          pickHelp.reduced (4), juce::Justification::centredLeft, 5);
    }
    if (tabs.getSelected() == 1 && message.isNotEmpty())
    {
        g.setColour (col::cyan);
        g.setFont (uiFont (10.5f, true));
        g.drawText (message, preview.getBounds().reduced (6, 3), juce::Justification::bottomRight);
    }
    if (tabs.getSelected() == 3)
    {
        g.setFont (uiFont (10.5f, true));
        g.setColour (smpError ? col::red : col::cyan);
        g.drawFittedText (smpMessage.isNotEmpty() ? smpMessage : (shownSample != nullptr ? juce::String::fromUTF8 (shownSample->name.c_str())
                                                                                        : T ("Datei hierher ziehen")),
                          smpInfo, juce::Justification::centredLeft, 1);
    }
    if (dragOver)
    {
        g.setColour (col::violet.withAlpha (0.18f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f), 10.0f);
        g.setColour (col::cyan);
        g.setFont (uiFont (18.0f, true));
        g.drawText (tabs.getSelected() == 3 ? T ("Datei hier ablegen -> Bass-Sample") : T ("Datei hier ablegen -> eigene Wavetable"),
                    getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace samsi::ui
