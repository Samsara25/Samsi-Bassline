#include "SequencerPanel.h"

#include "../PluginProcessor.h"

namespace samsi::ui
{

namespace
{
    const char* kRowNames[StepGrid::NumRows] = { "KICK", "BASS", "TON", "LÄNGE", "VEL", "SLIDE" };
}

StepGrid::StepGrid (SamsiBasslineProcessor& p) : proc (p)
{
    setTooltip (T ("Klick = an/aus · Ziehen (TON/LÄNGE/VEL) = Wert ändern · Doppelklick = Standard · "
                                        "Rechtsklick auf BASS = Slide"));
}

juce::Rectangle<float> StepGrid::cell (int row, int step) const
{
    const auto r = getLocalBounds().toFloat();
    const float gw = r.getWidth() - labelWidth();
    const float gap = 6.0f;                          // Abstand zwischen Vierteln
    const int perBeat = numSteps / 4;
    const float cw = (gw - 3.0f * gap) / (float) numSteps;
    const float rh = r.getHeight() / (float) NumRows;
    const float x = r.getX() + labelWidth() + (float) step * cw + (float) (step / perBeat) * gap;
    return { x + 1.5f, r.getY() + (float) row * rh + 1.5f, cw - 3.0f, rh - 3.0f };
}

bool StepGrid::hit (juce::Point<float> p, int& row, int& step) const
{
    for (int rr = 0; rr < NumRows; ++rr)
        for (int s = 0; s < numSteps; ++s)
            if (cell (rr, s).expanded (1.5f).contains (p)) { row = rr; step = s; return true; }
    return false;
}

void StepGrid::paint (juce::Graphics& g)
{
    numSteps = stepsForGrid (proc.readSettings().global.seqGrid);
    const auto outOfKey = proc.analyzeKey().stepOutOfKey;
    const auto r = getLocalBounds().toFloat();
    const float rh = r.getHeight() / (float) NumRows;

    for (int row = 0; row < NumRows; ++row)
    {
        g.setColour (row == RowKick ? col::kick : row == RowSlide ? col::amber : col::violet.brighter (0.4f));
        g.setFont (uiFont (12.0f, true));
        g.drawText (T (kRowNames[row]), juce::Rectangle<float> (r.getX(), r.getY() + (float) row * rh, labelWidth() - 6.0f, rh),
                    juce::Justification::centredLeft);
    }

    const int perBeat = numSteps / 4;
    for (int s = 0; s < numSteps; ++s)
    {
        const Step st = proc.pattern.getStep (s);
        if (s == playStep)
        {
            auto col0 = cell (0, s).getUnion (cell (NumRows - 1, s)).expanded (1.5f);
            g.setColour (col::cyan.withAlpha (0.16f));
            g.fillRoundedRectangle (col0, 4.0f);
        }

        for (int row = 0; row < NumRows; ++row)
        {
            const auto c = cell (row, s);
            g.setColour (juce::Colour (s % perBeat == 0 ? 0xff16264a : 0xff101c3a));
            g.fillRoundedRectangle (c, 4.0f);

            bool on = false;
            float fillAmt = 0.0f;
            juce::String txt;
            juce::Colour colour = col::violet;
            switch (row)
            {
                case RowKick:  on = st.kick;  colour = col::kick;   fillAmt = (float) st.kickVel / 127.0f; break;
                case RowBass:  on = st.bass;  colour = col::violet.brighter (0.2f); fillAmt = 1.0f; break;
                case RowNote:  on = st.bass;  txt = (st.note > 0 ? "+" : "") + juce::String (st.note); break;
                case RowLen:   on = st.bass;  fillAmt = juce::jmin (1.0f, (float) st.lenPct / 100.0f);
                               txt = juce::String (st.lenPct); break;
                case RowVel:   on = st.bass;  fillAmt = (float) st.bassVel / 127.0f; break;
                case RowSlide: on = st.bass && st.slide; colour = col::amber; fillAmt = 1.0f; break;
                default: break;
            }

            if (row == RowKick || row == RowBass || row == RowSlide)
            {
                if (on)
                {
                    g.setColour (colour.withAlpha (0.3f));
                    g.fillRoundedRectangle (c.expanded (1.0f), 5.0f);
                    g.setGradientFill (juce::ColourGradient (colour.brighter (0.4f), c.getX(), c.getY(),
                                                             colour.darker (0.4f), c.getX(), c.getBottom(), false));
                    g.fillRoundedRectangle (c.withTrimmedTop (c.getHeight() * (1.0f - (0.35f + 0.65f * fillAmt))), 4.0f);
                }
            }
            else if (on)
            {
                if (row != RowNote)
                {
                    g.setColour (col::violet.withAlpha (0.55f));
                    g.fillRoundedRectangle (c.withTrimmedTop (c.getHeight() * (1.0f - fillAmt)), 4.0f);
                }
                else if (outOfKey[(size_t) s])
                {
                    // Ton passt nicht zur Tonart: rot markieren
                    g.setColour (col::red.withAlpha (0.55f));
                    g.fillRoundedRectangle (c, 4.0f);
                    g.setColour (col::red);
                    g.drawRoundedRectangle (c.reduced (0.5f), 4.0f, 1.2f);
                }
                else if (st.note != 0)
                {
                    g.setColour (col::magenta.withAlpha (0.25f + 0.03f * (float) std::abs (st.note)));
                    g.fillRoundedRectangle (c, 4.0f);
                }
                if (txt.isNotEmpty())
                {
                    g.setColour (col::text);
                    g.setFont (uiFont (numSteps > 16 ? 9.0f : 11.0f, true));
                    g.drawText (txt, c, juce::Justification::centred);
                }
            }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    int row, s;
    dragRow = -1;
    if (! hit (e.position, row, s)) return;
    Step st = proc.pattern.getStep (s);

    if (e.mods.isPopupMenu() && row == RowBass)
    {
        st.slide = ! st.slide;
        proc.setStep (s, st);
        repaint();
        return;
    }

    dragRow = row;
    dragStep = s;
    dragStartY = e.position.y;

    switch (row)
    {
        case RowKick:  st.kick = ! st.kick;  paintValue = st.kick; break;
        case RowBass:  st.bass = ! st.bass;  paintValue = st.bass; break;
        case RowSlide: st.slide = ! st.slide; if (st.slide) st.bass = true; paintValue = st.slide; break;
        case RowNote:  dragStartValue = st.note; break;
        case RowLen:   dragStartValue = st.lenPct; break;
        case RowVel:   dragStartValue = st.bassVel; break;
        default: break;
    }
    proc.setStep (s, st);
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRow < 0) return;
    const float dy = dragStartY - e.position.y;

    if (dragRow == RowKick || dragRow == RowBass || dragRow == RowSlide)
    {
        int row, s;
        if (hit ({ e.position.x, cell (dragRow, 0).getCentreY() }, row, s))
        {
            Step st = proc.pattern.getStep (s);
            if (dragRow == RowKick)  st.kick = paintValue;
            if (dragRow == RowBass)  st.bass = paintValue;
            if (dragRow == RowSlide) { st.slide = paintValue; if (paintValue) st.bass = true; }
            proc.setStep (s, st);
            repaint();
        }
        return;
    }

    Step st = proc.pattern.getStep (dragStep);
    if (dragRow == RowNote) st.note    = juce::jlimit (-24, 24, dragStartValue + juce::roundToInt (dy / 8.0f));
    if (dragRow == RowLen)  st.lenPct  = juce::jlimit (10, 400, dragStartValue + 5 * juce::roundToInt (dy / 3.0f));
    if (dragRow == RowVel)  st.bassVel = juce::jlimit (1, 127, dragStartValue + juce::roundToInt (dy));
    st.bass = true;
    proc.setStep (dragStep, st);
    repaint();
}

void StepGrid::mouseUp (const juce::MouseEvent&) { dragRow = -1; }

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    int row, s;
    if (! hit (e.position, row, s)) return;
    Step st = proc.pattern.getStep (s);
    if (row == RowNote) st.note = 0;
    if (row == RowLen)  st.lenPct = 75;
    if (row == RowVel)  st.bassVel = 100;
    proc.setStep (s, st);
    repaint();
}

//==============================================================================
SequencerPanel::SequencerPanel (SamsiBasslineProcessor& p, juce::AudioProcessorValueTreeState& apvts)
    : PanelFrame ("SEQUENCER", col::blue.brighter (0.4f)), proc (p), grid (p),
      seqOn (apvts, pid::seqOn, "Sequencer an"),
      swing (apvts, pid::swing, "Swing", col::amber),
      root (apvts, pid::root, "Grundton", col::violet.brighter (0.3f)),
      midiKick (apvts, pid::midiKick, "MIDI-Kick", col::kick),
      mode (apvts, pid::seqMode, "Modus (Abspielen)"),
      lookahead (apvts, pid::lookahead, "MIDI-Lookahead"),
      gridChoice (apvts, pid::seqGrid, "Raster")
{
    for (auto* c : std::initializer_list<juce::Component*> { &grid, &seqOn, &swing, &root, &midiKick, &presetKBBB, &presetOff,
                                                             &presetClear, &mode, &lookahead, &gridChoice })
        addAndMakeVisible (c);
    mode.setTooltip (T ("Transport: läuft mit Play des Hosts. MIDI-Gate: läuft, solange eine Note gehalten wird. "
                        "Gate + Transpose: die gehaltene Note bestimmt zusätzlich den Bass-Grundton."));
    lookahead.setTooltip (T ("Vorausschau für Live-MIDI: erlaubt negativen Bass-Versatz. Meldet die Verzögerung als "
                             "Latenz an den Host (wird automatisch ausgeglichen)."));

    gridChoice.setTooltip (T ("Raster des Sequencers: 1/16 (16 Schritte), 1/16-Triole (24, rollender Galopp) oder 1/32 (32). "
                              "Swing wirkt nur auf 1/16 und 1/32."));
    presetKBBB.onClick  = [this] { proc.setPattern (PatternData::makeRolling (proc.readSettings().global.seqGrid, false)); grid.refresh(); };
    presetOff.onClick   = [this] { proc.setPattern (PatternData::makeRolling (proc.readSettings().global.seqGrid, true)); grid.refresh(); };
    presetClear.onClick = [this] { proc.setPattern (PatternData::makeEmpty()); grid.refresh(); };
    presetKBBB.setTooltip (T ("Rollendes Grundmuster passend zum Raster (1/16: K-B-B-B, Triole: K-.-B-B-B-B, 1/32: K-.-.-B-B-B-B-B)"));
    presetOff.setTooltip (T ("Offbeat-Variante passend zum Raster (1/16: K-.-B-B)"));
    presetKBBB.setButtonText ("ROLL");
}

void SequencerPanel::resized()
{
    auto r = content();
    auto controls = r.removeFromBottom (122);
    grid.setBounds (r.reduced (0, 2));

    // Reihe 1: Ein/Aus + Muster | Swing, Grundton, MIDI-Kick
    auto row1 = controls.removeFromTop (72);
    auto left = row1.removeFromLeft (row1.getWidth() - 228);
    seqOn.setBounds (left.removeFromTop (28).reduced (2, 2));
    left.removeFromTop (8);
    auto btns = left.removeFromTop (28);
    const int bw = btns.getWidth() / 3;
    for (auto* b : { &presetKBBB, &presetOff, &presetClear })
        b->setBounds (btns.removeFromLeft (bw).reduced (2, 0));
    const int kw = row1.getWidth() / 3;
    swing.setBounds (row1.removeFromLeft (kw));
    root.setBounds (row1.removeFromLeft (kw));
    midiKick.setBounds (row1);

    // Reihe 2: Raster, Modus, Lookahead
    controls.removeFromTop (4);
    const int cw = controls.getWidth() / 3;
    gridChoice.setBounds (controls.removeFromLeft (cw).reduced (3, 0));
    mode.setBounds (controls.removeFromLeft (cw).reduced (3, 0));
    lookahead.setBounds (controls.reduced (3, 0));
}

} // namespace samsi::ui
