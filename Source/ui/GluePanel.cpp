#include "GluePanel.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace samsi::ui
{

GluePanel::GluePanel (SamsiBasslineProcessor& p)
    : proc (p),
      on (p.apvts, pid::gOn, "Glue an"), compOn (p.apvts, pid::gcOn, "Kompressor"),
      b1On (p.apvts, pid::d1On, "Band 1"), b2On (p.apvts, pid::d2On, "Band 2"),
      sub ({ "KOMPRESSOR", "DYN-EQ 1", "DYN-EQ 2" }, col::amber),
      style (p.apvts, pid::gcStyle, "Stil"), b1Type (p.apvts, pid::d1Type, "Typ"), b2Type (p.apvts, pid::d2Type, "Typ"),
      thr (p.apvts, pid::gcThr, "Threshold", col::amber), ratio (p.apvts, pid::gcRatio, "Ratio", col::amber),
      att (p.apvts, pid::gcAtt, "Attack", col::amber), rel (p.apvts, pid::gcRel, "Release", col::amber),
      knee (p.apvts, pid::gcKnee, "Knee", col::amber), range (p.apvts, pid::gcRange, "Range", col::amber),
      scHp (p.apvts, pid::gcScHp, "SC-Hochpass", col::turq), scLp (p.apvts, pid::gcScLp, "SC-Tiefpass", col::turq),
      mix (p.apvts, pid::gcMix, "Mix", col::amber),
      b1Freq (p.apvts, pid::d1Freq, "Frequenz", col::cyan), b1Q (p.apvts, pid::d1Q, "Q", col::cyan),
      b1Range (p.apvts, pid::d1Range, "Range", col::cyan), b1Thr (p.apvts, pid::d1Thr, "Threshold", col::cyan),
      b1Att (p.apvts, pid::d1Att, "Attack", col::cyan), b1Rel (p.apvts, pid::d1Rel, "Release", col::cyan),
      b2Freq (p.apvts, pid::d2Freq, "Frequenz", col::violet.brighter (0.3f)), b2Q (p.apvts, pid::d2Q, "Q", col::violet.brighter (0.3f)),
      b2Range (p.apvts, pid::d2Range, "Range", col::violet.brighter (0.3f)), b2Thr (p.apvts, pid::d2Thr, "Threshold", col::violet.brighter (0.3f)),
      b2Att (p.apvts, pid::d2Att, "Attack", col::violet.brighter (0.3f)), b2Rel (p.apvts, pid::d2Rel, "Release", col::violet.brighter (0.3f))
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &sub, &autoButton })
        addAndMakeVisible (c);
    for (auto* c : std::initializer_list<juce::Component*> { &compOn, &b1On, &b2On, &style, &b1Type, &b2Type, &thr, &ratio, &att, &rel, &knee,
                                                             &range, &scHp, &scLp, &mix, &b1Freq, &b1Q, &b1Range, &b1Thr, &b1Att, &b1Rel,
                                                             &b2Freq, &b2Q, &b2Range, &b2Thr, &b2Att, &b2Rel })
        addChildComponent (c);
    on.get().setTooltip (T ("Kick→Bass-Glue: die Kick steuert einen Sidechain-Kompressor und zwei dynamische EQ-Bänder auf dem Bass. "
                            "Wirkt sample-genau ohne Latenz und nur auf den Bass (MIX = KICK + BASS bleibt erhalten). Auto Fit misst inklusive Glue."));
    autoButton.setTooltip (T ("Startwerte aus dem aktuellen Klang: Band 1 auf den Kick-Grundton, Band 2 auf den ersten Oberton, "
                              "Thresholds relativ zum gemessenen Kick-Pegel, Release passend zum Tempo. Danach Auto Fit (Orb) drücken."));
    autoButton.onClick = [this] { message = proc.autoSetupGlue(); repaint(); };
    sub.onChange = [this] (int s) { setSub (s); };
    thr.slider().setTooltip (T ("Ab diesem Kick-Pegel (nach SC-Filter) wird der Bass leiser"));
    range.slider().setTooltip (T ("Maximale Pegelreduktion des Basses – leichte Sidechain-Kompression: 2–6 dB"));
    scLp.slider().setTooltip (T ("Sidechain-Tiefpass: nur der Kick-Körper löst aus, nicht der Click"));
    b1Range.slider().setTooltip (T ("Dynamischer EQ: so viel wird das Band im Bass abgesenkt (negativ) oder angehoben, solange die Kick in diesem Band klingt"));
    setSub (0);
}

void GluePanel::setSub (int s)
{
    for (auto* c : std::initializer_list<juce::Component*> { &compOn, &style, &thr, &ratio, &att, &rel, &knee, &range, &scHp, &scLp, &mix })
        c->setVisible (s == 0);
    for (auto* c : std::initializer_list<juce::Component*> { &b1On, &b1Type, &b1Freq, &b1Q, &b1Range, &b1Thr, &b1Att, &b1Rel })
        c->setVisible (s == 1);
    for (auto* c : std::initializer_list<juce::Component*> { &b2On, &b2Type, &b2Freq, &b2Q, &b2Range, &b2Thr, &b2Att, &b2Rel })
        c->setVisible (s == 2);
    repaint();
}

void GluePanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (26);
    on.setBounds (top.removeFromLeft (104).reduced (0, 2));
    top.removeFromLeft (6);
    autoButton.setBounds (top.removeFromRight (64).reduced (0, 2));
    top.removeFromRight (6);
    sub.setBounds (top.reduced (0, 2));
    r.removeFromTop (4);
    curveArea = r.removeFromTop (std::max (60, r.getHeight() - 2 * 66 - 6));
    r.removeFromTop (6);
    auto row1 = r.removeFromTop (66), row2 = r.removeFromTop (66);
    const int kw = row1.getWidth() / 5;

    // Kompressor
    layoutGrid (row1, { &thr, &ratio, &att, &rel, &knee }, 5, 66);
    {
        auto rr = row2;
        range.setBounds (rr.removeFromLeft (kw));
        mix.setBounds (rr.removeFromLeft (kw));
        scHp.setBounds (rr.removeFromLeft (kw));
        scLp.setBounds (rr.removeFromLeft (kw));
        compOn.setBounds (rr.removeFromTop (28).reduced (2, 3));
        style.setBounds (rr.reduced (2, 0));
    }
    // Bänder
    auto bandLayout = [&] (Toggle& o, Choice& t, Knob& f, Knob& q, Knob& rg, Knob& th, Knob& a, Knob& re)
    {
        auto r1 = row1;
        auto left = r1.removeFromLeft (kw + 20);
        o.setBounds (left.removeFromTop (28).reduced (2, 3));
        t.setBounds (left.reduced (2, 0));
        layoutGrid (r1, { &f, &q, &rg }, 3, 66);
        layoutGrid (row2, { &th, &a, &re }, 3, 66);
    };
    bandLayout (b1On, b1Type, b1Freq, b1Q, b1Range, b1Thr, b1Att, b1Rel);
    bandLayout (b2On, b2Type, b2Freq, b2Q, b2Range, b2Thr, b2Att, b2Rel);
}

void GluePanel::refresh()
{
    const float gr = proc.takeGlueCompGrDb();
    grShown = std::max (gr, grShown * 0.85f);
    grHold = std::max (grHold * 0.995f, gr);
    repaint (curveArea.expanded (2));
}

void GluePanel::paint (juce::Graphics& g)
{
    const auto s = proc.readSettings().glue;
    auto r = curveArea.toFloat();
    g.setColour (juce::Colour (0xd0010506));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::amber.withAlpha (0.25f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto meter = r.removeFromRight (54.0f).reduced (6.0f, 6.0f);
    auto plot = r.reduced (6.0f, 6.0f);
    const float lo = std::log (20.0f), hi = std::log (1000.0f);
    auto xOf = [&] (float hz) { return plot.getX() + plot.getWidth() * (std::log (hz) - lo) / (hi - lo); };
    auto yOf = [&] (float db) { return plot.getCentreY() - db / 12.0f * plot.getHeight() * 0.5f; };

    g.setFont (uiFont (9.5f));
    for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f })
    {
        g.setColour (juce::Colour (0x22a8fff4));
        g.drawVerticalLine ((int) xOf (hz), plot.getY(), plot.getBottom());
        g.setColour (col::textDim);
        g.drawText (juce::String ((int) hz), juce::Rectangle<float> (xOf (hz) - 14.0f, plot.getBottom() - 11.0f, 28.0f, 10.0f), juce::Justification::centred);
    }
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f })
    {
        g.setColour (db == 0.0f ? col::text.withAlpha (0.35f) : juce::Colour (0x22a8fff4));
        g.drawHorizontalLine ((int) yOf (db), plot.getX(), plot.getRight());
        g.setColour (col::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() + 2.0f, yOf (db) - 11.0f, 24.0f, 10.0f), juce::Justification::left);
    }

    // Antwort der dynamischen Bänder (Darstellung mit der Näherung einer analogen Glocke/Shelf)
    auto bandDb = [] (const DynBandSettings& b, float gainDb, float hz)
    {
        if (! b.on) return 0.0f;
        const float x = hz / std::max (1.0f, b.freqHz);
        if (b.type == 1) return gainDb / (1.0f + std::pow (x, 2.0f * std::max (0.5f, b.q)));
        const float d = b.q * (x - 1.0f / x);
        return gainDb / (1.0f + d * d);
    };
    const float live[2] = { proc.getGlueBandGainDb (0), proc.getGlueBandGainDb (1) };
    const juce::Colour cols[2] = { col::cyan, col::violet.brighter (0.3f) };
    for (int pass = 0; pass < 2; ++pass)   // 0: Ziel bei voller Auslösung (gestrichelt), 1: momentan
    {
        juce::Path pth;
        for (int xi = 0; xi <= (int) plot.getWidth(); xi += 2)
        {
            const float hz = std::exp (lo + (hi - lo) * (float) xi / plot.getWidth());
            float db = 0.0f;
            for (int b = 0; b < 2; ++b)
                db += bandDb (s.band[b], pass == 0 ? s.band[b].rangeDb : (s.on ? live[b] : 0.0f), hz);
            const float y = yOf (juce::jlimit (-12.0f, 12.0f, db));
            if (xi == 0) pth.startNewSubPath (plot.getX() + (float) xi, y); else pth.lineTo (plot.getX() + (float) xi, y);
        }
        if (pass == 0)
        {
            juce::Path dashed;
            const float dl[2] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, pth, dl, 2);
            g.setColour (col::textDim.withAlpha (0.6f));
            g.fillPath (dashed);
        }
        else
            sym::glowStroke (g, pth, col::amber, 1.8f, 0.6f);
    }
    for (int b = 0; b < 2; ++b)
        if (s.band[b].on)
        {
            const float x = xOf (s.band[b].freqHz), y = yOf (juce::jlimit (-12.0f, 12.0f, s.on ? live[b] : 0.0f));
            g.setColour (cols[b]);
            g.fillEllipse (x - 5.0f, y - 5.0f, 10.0f, 10.0f);
            g.setColour (col::bgDeep);
            g.setFont (uiFont (9.0f, true));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x - 5.0f, y - 5.0f, 10.0f, 10.0f), juce::Justification::centred);
        }

    // Kompressor-Reduktion
    g.setColour (juce::Colour (0xff0b1a1c));
    g.fillRoundedRectangle (meter, 3.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, grShown / 12.0f);
    g.setColour (col::amber);
    g.fillRoundedRectangle (meter.withHeight (meter.getHeight() * frac), 3.0f);
    g.setColour (col::text);
    g.fillRect (meter.getX(), meter.getY() + meter.getHeight() * juce::jlimit (0.0f, 1.0f, grHold / 12.0f) - 1.0f, meter.getWidth(), 2.0f);
    g.setFont (uiFont (10.0f, true));
    g.setColour (col::textDim);
    g.drawFittedText ("GR\n" + juce::String (grShown, 1), meter.toNearestInt().removeFromBottom (26), juce::Justification::centred, 2);

    if (! s.on)
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (12.0f, true));
        g.drawText (T ("Glue aus – AUTO richtet Startwerte ein"), plot, juce::Justification::centred);
    }
    else if (message.isNotEmpty())
    {
        g.setColour (col::cyan);
        g.setFont (uiFont (10.0f));
        g.drawFittedText (message, plot.toNearestInt().removeFromTop (14).translated (28, 0), juce::Justification::left, 1);
    }
}

} // namespace samsi::ui
