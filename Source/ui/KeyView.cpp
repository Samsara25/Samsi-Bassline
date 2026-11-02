#include "KeyView.h"

#include "../PluginProcessor.h"

namespace samsi::ui
{

namespace
{
    constexpr double kLo = 20.0, kHi = 250.0;
}

juce::Colour KeyView::statusColour (Status s)
{
    switch (s)
    {
        case Status::Ok:   return col::green;
        case Status::Warn: return col::amber;
        case Status::Bad:  return col::red;
        case Status::Off:  break;
    }
    return col::textDim;
}

KeyView::KeyView (SamsiBasslineProcessor& p)
    : proc (p),
      keyRoot (p.apvts, pid::keyRoot, "Tonart"),
      keyScale (p.apvts, pid::keyScale, "Skala"),
      tolerance (p.apvts, pid::keyTol, "Toleranz", col::amber)
{
    for (auto* c : std::initializer_list<juce::Component*> { &keyRoot, &keyScale, &tolerance, &fixKick, &fixBass, &fixSample, &fixPattern })
        addAndMakeVisible (c);
    fixPattern.setButtonText (T ("TÖNE > SKALA"));
    fixKick.setTooltip (T ("Kick-Grundton auf den Grundton der Tonart setzen (nächste Oktave 30–70 Hz)"));
    fixBass.setTooltip (T ("Bass-Grundton (Sequencer) auf den Grundton der Tonart setzen"));
    fixSample.setTooltip (T ("Kick-Sample so stimmen, dass sein erkannter Grundton zum Kick-Grundton passt"));
    fixPattern.setTooltip (T ("Pattern-Töne außerhalb der Skala um einen Halbton verschieben"));
    fixKick.onClick    = [this] { proc.snapKickToKey(); update(); };
    fixBass.onClick    = [this] { proc.snapBassToKey(); update(); };
    fixSample.onClick  = [this] { proc.snapSampleToKick(); update(); };
    fixPattern.onClick = [this] { proc.snapPatternToScale(); update(); };
    update();
}

void KeyView::update()
{
    report = proc.analyzeKey();
    const auto k = proc.getKickSample();
    fixSample.setEnabled (k != nullptr && k->pitchHz > 0.0f);
    repaint();
}

void KeyView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (50);
    keyRoot.setBounds (top.removeFromLeft (110).reduced (2, 2));
    keyScale.setBounds (top.removeFromLeft (190).reduced (2, 2));
    tolerance.setBounds (top.removeFromLeft (80));
    auto buttons = r.removeFromBottom (28).reduced (0, 2);
    const int bw = buttons.getWidth() / 4;
    for (auto* b : { &fixKick, &fixBass, &fixSample, &fixPattern })
        b->setBounds (buttons.removeFromLeft (bw).reduced (2, 0));
    r.removeFromTop (4);
    ladder = r.removeFromTop (124);
    r.removeFromTop (6);
    checks = r.reduced (0, 2);
}

float KeyView::hzToX (double hz) const
{
    const auto a = ladder.toFloat().reduced (8.0f, 0.0f);
    return a.getX() + a.getWidth() * (float) (std::log (juce::jlimit (kLo, kHi, hz) / kLo) / std::log (kHi / kLo));
}

void KeyView::paint (juce::Graphics& g)
{
    const auto s = proc.readSettings();
    const int key = s.global.keyRoot, scale = s.global.keyScale;
    const auto L = ladder.toFloat();

    g.setColour (juce::Colour (0xc0020809));
    g.fillRoundedRectangle (L, 8.0f);

    // Halbton-Felder: in der Skala leuchtend, Grundton hell, außerhalb dunkel
    const float stripTop = L.getY() + 46.0f, stripH = 26.0f;
    for (int midi = 15; midi <= 60; ++midi)
    {
        const double f0 = midiToHz (midi - 0.5), f1 = midiToHz (midi + 0.5);
        if (f1 < kLo || f0 > kHi) continue;
        const float x0 = hzToX (f0), x1 = hzToX (f1);
        const int pc = midi % 12;
        const bool in = inScale (pc, key, scale);
        const bool root = pc == key;
        g.setColour (root ? col::cyan.withAlpha (0.55f) : in ? col::turq.withAlpha (0.22f) : juce::Colour (0xff0b1517));
        g.fillRect (juce::Rectangle<float> (x0 + 0.5f, stripTop, x1 - x0 - 1.0f, stripH));
        if (root)
        {
            g.setColour (col::cyan);
            g.setFont (uiFont (10.0f, true));
            g.drawText (describeHz (midiToHz (midi), key, scale).name, juce::Rectangle<float> (x0 - 10.0f, stripTop + stripH + 1.0f, x1 - x0 + 20.0f, 12.0f),
                        juce::Justification::centred);
        }
    }

    // Frequenzachse
    g.setFont (uiFont (10.0f));
    for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0, 250.0 })
    {
        const float x = hzToX (f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, L.getY() + 4.0f, stripTop);
        g.setColour (col::textDim);
        g.drawText (juce::String ((int) f), juce::Rectangle<float> (x - 15.0f, L.getBottom() - 14.0f, 30.0f, 12.0f), juce::Justification::centred);
    }

    // Obertöne des Bass-Grundtons (fein)
    for (int h = 2; h <= 6; ++h)
    {
        const double f = report.bassRoot.hz * h;
        if (f > kHi) break;
        g.setColour (col::violet.withAlpha (0.35f));
        g.fillRect (hzToX (f) - 0.5f, stripTop + stripH - 8.0f, 1.0f, 8.0f);
    }

    auto marker = [&] (double hz, const juce::String& label, juce::Colour c, float row)
    {
        const float x = hzToX (hz);
        const float y = L.getY() + 6.0f + row * 13.0f;
        g.setColour (c.withAlpha (0.35f));
        g.drawLine (x, y + 6.0f, x, stripTop + stripH, 1.0f);
        g.setColour (c);
        g.fillPath (sym::drop ({ x, stripTop + stripH * 0.5f }, 11.0f, juce::MathConstants<float>::pi));
        g.setFont (uiFont (10.5f, true));
        g.drawText (label, juce::Rectangle<float> (x - 70.0f, y - 1.0f, 140.0f, 12.0f), juce::Justification::centred);
    };

    const auto& ch = report.checks;
    const auto stat = [&ch] (size_t i) { return i < ch.size() ? ch[i].status : Status::Off; };

    // Pattern-Töne
    for (int i = 0; i < stepsForGrid (s.global.seqGrid); ++i)
    {
        const Step st = proc.pattern.getStep (i);
        if (! st.bass) continue;
        const double hz = midiToHz (s.global.rootNote + st.note);
        const float x = hzToX (hz);
        g.setColour (report.stepOutOfKey[(size_t) i] ? col::red : col::violet.brighter (0.4f));
        g.fillEllipse (x - 3.0f, stripTop + stripH + 13.0f, 6.0f, 6.0f);
    }

    marker (report.bassRoot.hz, "BASS " + juce::String (report.bassRoot.hz, 1) + " Hz", statusColour (stat (1)), 1.0f);
    marker (report.kick.hz, "KICK " + juce::String (report.kick.hz, 1) + " Hz", statusColour (stat (0)), 0.0f);
    if (report.sampleValid)
        marker (report.sample.hz, "SAMPLE " + juce::String (report.sample.hz, 1) + " Hz", statusColour (stat (4)), 2.0f);

    // Prüfliste
    auto c = checks;
    const int rowH = juce::jmax (24, c.getHeight() / juce::jmax (1, (int) ch.size()));
    for (const auto& item : ch)
    {
        auto row = c.removeFromTop (rowH);
        const auto colr = statusColour (item.status);
        auto led = row.removeFromLeft (18).toFloat().withSizeKeepingCentre (10.0f, 10.0f).translated (0.0f, -4.0f);
        g.setColour (colr.withAlpha (0.3f));
        g.fillEllipse (led.expanded (3.0f));
        g.setColour (colr);
        g.fillEllipse (led);
        g.setFont (uiFont (12.0f, true));
        g.setColour (item.status == Status::Bad ? col::red : col::text);
        g.drawText (juce::String::fromUTF8 (item.title.c_str()), row.removeFromTop (14), juce::Justification::centredLeft);
        g.setFont (uiFont (10.5f));
        g.setColour (col::textDim);
        g.drawFittedText (juce::String::fromUTF8 (item.detail.c_str()), row, juce::Justification::topLeft, 1);
    }
}

} // namespace samsi::ui
