#include "FitPanel.h"

#include "../PluginProcessor.h"

namespace samsi::ui
{

FitPanel::FitPanel (SamsiBasslineProcessor& p, juce::AudioProcessorValueTreeState& apvts)
    : PanelFrame ("AUTO FIT · DETAILS", col::amber), proc (p),
      tabs ({ "STEUERUNG", "MESSWERTE", "GLUE" }, col::amber),
      glue (p),
      loudMatch (apvts, pid::loudMatch, "A/B lautheitsangepasst"),
      lockKick (apvts, pid::lockKick, "Lock Kick"),
      lockTiming (apvts, pid::lockTiming, "Lock Timing"),
      lockBassTone (apvts, pid::lockBassTone, "Lock Bass Tone"),
      polarity (apvts, pid::fPolarity, "Polarität invertiert"),
      duckLow (apvts, pid::fDuckLow, "Ducking nur Tiefband"),
      maxShift (apvts, pid::maxShift, "Max. Versatz", col::amber),
      phase (apvts, pid::fPhase, "Bass-Phase", col::amber),
      offset (apvts, pid::fOffset, "Versatz", col::amber),
      duckDb (apvts, pid::fDuckDb, "Duck-Tiefe", col::amber),
      duckMs (apvts, pid::fDuckMs, "Duck-Zeit", col::amber),
      duckHz (apvts, pid::fDuckHz, "Duck-Freq.", col::amber),
      trim (apvts, pid::fTrim, "Bass-Trim", col::amber),
      attAdd (apvts, pid::fAttAdd, "Attack +", col::amber),
      kickTail (apvts, pid::fKickTail, "Kick-Tail", col::amber),
      phaseMode (apvts, pid::fPhaseMode, "Phasenmodus")
{
    for (auto* c : std::initializer_list<juce::Component*> { &tabs, &undoButton, &abButton, &loudMatch, &lockKick,
                                                             &lockTiming, &lockBassTone, &polarity, &duckLow, &maxShift, &phase,
                                                             &offset, &duckDb, &duckMs, &duckHz, &trim, &attAdd, &kickTail, &phaseMode })
        addAndMakeVisible (c);

    tabs.onChange = [this] (int page) { setPage (page); };
    addChildComponent (glue);
    undoButton.onClick = [this] { proc.undoFit(); update(); };

    abButton.setClickingTogglesState (true);
    abAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, pid::fEnable, abButton);
    abButton.onStateChange = [this]
    {
        abButton.setButtonText (abButton.getToggleState() ? "NACHHER (Fit aktiv)" : "VORHER (Fit aus)");
    };
    abButton.setTooltip ("A/B-Vergleich. Mit 'lautheitsangepasst' wird VORHER auf die Lautheit von NACHHER angeglichen.");
    undoButton.setTooltip (T ("Letztes Auto-Fit-Ergebnis zurücknehmen (mehrstufig)."));
    setPage (0);
    update();
}

void FitPanel::setPage (int page)
{
    for (auto* c : std::initializer_list<juce::Component*> { &undoButton, &abButton, &loudMatch, &lockKick, &lockTiming, &lockBassTone,
                                                             &polarity, &duckLow, &maxShift, &phase, &offset, &duckDb, &duckMs,
                                                             &duckHz, &trim, &attAdd, &kickTail, &phaseMode })
        c->setVisible (page == 0);
    glue.setVisible (page == 2);
    repaint();
}

void FitPanel::update()
{
    running = proc.analysis.isFitRunning();
    progress = proc.analysis.getFitProgress();
    const auto fit = proc.getLastFit();
    undoButton.setEnabled (proc.canUndoFit());

    juce::String s;
    juce::Colour c = col::textDim;
    if (running)                     { s = T ("Analyse läuft: ") + juce::String (juce::roundToInt (progress * 100.0f)) + " %"; c = col::cyan; }
    else if (fit == nullptr)         { s = T ("Noch keine Analyse in dieser Sitzung. Orb in der Mitte klicken."); }
    else if (proc.isFitStale())      { s = T ("Analyse veraltet: Klang/Pattern/Tempo geändert. Neu analysieren."); c = col::amber; }
    else                             { s = "Analyse aktuell (" + juce::String (fit->evaluations) + " Kandidaten gerendert)."; c = col::green; }

    if (s != status || c != statusColour || fit != shownFit || running)
    {
        status = s;
        statusColour = c;
        shownFit = fit;
        repaint();
    }
}

void FitPanel::resized()
{
    auto r = content();
    tabs.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    pageArea = r;
    glue.setBounds (r);

    // Seite STEUERUNG – links: A/B, Undo, Sperren
    auto left = r.removeFromLeft (180);
    auto ab = left.removeFromTop (32).reduced (2);
    undoButton.setBounds (ab.removeFromRight (56));
    ab.removeFromRight (4);
    abButton.setBounds (ab);
    loudMatch.setBounds (left.removeFromTop (24).reduced (2));
    labelLocks = left.removeFromTop (16);
    lockKick.setBounds (left.removeFromTop (24).reduced (2));
    lockTiming.setBounds (left.removeFromTop (24).reduced (2));
    lockBassTone.setBounds (left.removeFromTop (24).reduced (2));
    maxShift.setBounds (left.removeFromTop (70).withSizeKeepingCentre (90, 70));
    statusArea = left;

    r.removeFromLeft (8);

    // rechts: Ergebnisse (manuell übersteuerbar)
    auto mid = r;
    labelResult = mid.removeFromTop (14);
    const int rowH = 70;
    layoutGrid (mid.removeFromTop (rowH), { &phase, &offset, &duckDb, &duckMs }, 4, rowH);
    layoutGrid (mid.removeFromTop (rowH), { &duckHz, &trim, &attAdd, &kickTail }, 4, rowH);
    auto tg = mid.removeFromTop (26);
    polarity.setBounds (tg.removeFromLeft (tg.getWidth() / 2).reduced (2));
    duckLow.setBounds (tg.reduced (2));
    phaseMode.setBounds (mid.removeFromTop (46).withSizeKeepingCentre (200, 44));
    notesArea = mid;

    // Seite MESSWERTE
    tableArea = pageArea;
}

void FitPanel::paint (juce::Graphics& g)
{
    PanelFrame::paint (g);
    if (tabs.getSelected() == 0)
    {
        drawSectionLabel (g, labelLocks, "SCHUTZ / GRENZEN", col::amber);
        drawSectionLabel (g, labelResult, T ("ERGEBNIS · MANUELL ÜBERSTEUERBAR"), col::amber);
    }

    // Status und Fortschritt
    auto sa = (tabs.getSelected() == 0 ? statusArea : juce::Rectangle<int>()).reduced (2);
    if (running)
    {
        auto bar = sa.removeFromTop (8).toFloat();
        g.setColour (juce::Colour (0xff111c38));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (col::cyan);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * progress), 3.0f);
        sa.removeFromTop (4);
    }
    g.setColour (statusColour);
    g.setFont (uiFont (12.0f, true));
    g.drawFittedText (status, sa, juce::Justification::topLeft, 4);

    const auto fit = shownFit;
    if (tabs.getSelected() == 2) return;
    if (tabs.getSelected() == 0)
    {
        // Hinweise / Kompromisse unter den Ergebnis-Reglern
        if (fit != nullptr)
        {
            auto n = notesArea.reduced (2);
            g.setFont (uiFont (11.0f));
            for (size_t i = 1; i < fit->notes.size() && n.getHeight() > 12; ++i)
            {
                g.setColour (col::cyan.withAlpha (0.85f));
                const auto txt = juce::String::fromUTF8 (fit->notes[i].c_str());
                const int lines = juce::jmin (3, 1 + juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f), txt) / juce::jmax (1, n.getWidth()));
                g.drawFittedText (T ("• ") + txt, n.removeFromTop (13 * lines + 2), juce::Justification::topLeft, lines);
            }
        }
        return;
    }

    // Messwerte vorher / nachher
    auto t = tableArea;
    g.setFont (uiFont (11.5f, true));
    auto head = t.removeFromTop (16);
    g.setColour (col::textDim);
    g.drawText ("MESSWERT", head, juce::Justification::centredLeft);
    g.drawText ("NACHHER", head.removeFromRight (62), juce::Justification::centredRight);
    g.drawText ("VORHER", head.removeFromRight (62), juce::Justification::centredRight);

    struct Row { const char* name; double before, after; int dec; const char* unit; bool lowerIsBetter; };
    std::vector<Row> rows;
    if (fit != nullptr)
    {
        const auto& b = fit->before; const auto& a = fit->after;
        rows = {
            { "Auslöschung 20–60 Ø",  b.cancelMeanDb[0], a.cancelMeanDb[0], 1, "dB", true },
            { "Auslöschung 20–60 max", b.cancelWorstDb[0], a.cancelWorstDb[0], 1, "dB", true },
            { "Auslöschung 60–120 Ø", b.cancelMeanDb[1], a.cancelMeanDb[1], 1, "dB", true },
            { "Einsatz-Auslöschung",   b.onsetCancelDb, a.onsetCancelDb, 1, "dB", true },
            { "Kick-Attack-Verlust",   b.kickAttackLossDb, a.kickAttackLossDb, 1, "dB", true },
            { "Kick/Bass am Einsatz",  b.onsetMaskDb, a.onsetMaskDb, 1, "dB", true },
            { "Summenpeak",            b.sumPeakDb, a.sumPeakDb, 1, "dBFS", true },
            { "Tiefband-Überhöhung",   b.lowBumpDb, a.lowBumpDb, 1, "dB", true },
            { "Bass-Energie",          b.bassEnergyDb, a.bassEnergyDb, 1, "dB", false },
            { "Noten-Streuung",        b.noteSigmaDb, a.noteSigmaDb, 2, "dB", true },
            { "Lautheit (K)",          b.loudnessLu, a.loudnessLu, 1, "LU", false },
        };
    }
    for (auto& row : rows)
    {
        auto line = t.removeFromTop (19);
        g.setFont (uiFont (11.5f));
        g.setColour (col::textDim);
        g.drawText (T (row.name), line, juce::Justification::centredLeft);
        const bool better = row.lowerIsBetter ? row.after < row.before - 0.05 : false;
        const bool worse  = row.lowerIsBetter ? row.after > row.before + 0.05 : false;
        g.setFont (uiFont (12.0f, true));
        g.setColour (better ? col::green : worse ? col::amber : col::text);
        g.drawText (juce::String (row.after, row.dec), line.removeFromRight (62), juce::Justification::centredRight);
        g.setColour (col::text.withAlpha (0.75f));
        g.drawText (juce::String (row.before, row.dec), line.removeFromRight (62), juce::Justification::centredRight);
    }
    if (fit != nullptr)
    {
        g.setFont (uiFont (10.5f));
        g.setColour (col::textDim);
        g.drawFittedText (T ("Auslöschung: Energieverlust der Summe gegenüber Leistungsaddition, in 10-ms-Fenstern "
                                                  "während der Kick klingt. Kein Gesamt-Score – jeder Wert ist eine echte Messung."),
                          t.reduced (0, 4), juce::Justification::topLeft, 3);
    }
    else
    {
        g.setFont (uiFont (12.0f));
        g.setColour (col::textDim);
        g.drawFittedText (T ("Messwerte erscheinen nach der ersten Analyse.\n\n"
                                                  "Ziel: kontrollierte Tiefton-Energie, wenig Auslöschung, begrenzte Peaks, "
                                                  "geschützter Kick-Attack, klare Bassnoten-Einsätze."),
                          t.reduced (0, 4), juce::Justification::topLeft, 8);
    }
}

} // namespace samsi::ui
