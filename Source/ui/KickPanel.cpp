#include "KickPanel.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"
#include "../content/KeyAnalysis.h"
#include "../content/SampleLoader.h"

namespace samsi::ui
{

namespace
{
    constexpr float kHzLo = 20.0f, kHzHi = 3000.0f;
}

KickEnvelopeEditor::KickEnvelopeEditor (juce::AudioProcessorValueTreeState& a) : apvts (a)
{
    cached = read();
    setTooltip (T ("Ziehpunkte: Pitch (violett) und Lautstärke (türkis). Doppelklick auf Punkt = Standardwert."));
    startTimerHz (30);
}

KickEnvelopeEditor::~KickEnvelopeEditor() { endGestures(); }

juce::RangedAudioParameter* KickEnvelopeEditor::param (const char* id) const { return apvts.getParameter (id); }

KickEnvelopeEditor::Values KickEnvelopeEditor::read() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    return { v (pid::kStart), v (pid::kEnd), v (pid::kPitchMs), v (pid::kPitchCurve), v (pid::kAttack),
             v (pid::kHold), v (pid::kDecay), v (pid::kDecayCurve), v (pid::kTailLevel), v (pid::kTailMs) };
}

void KickEnvelopeEditor::timerCallback()
{
    const auto v = read();
    if (std::memcmp (&v, &cached, sizeof v) != 0)
    {
        cached = v;
        if (dragging == None)
            viewMs = juce::jlimit (60.0f, 3000.0f,
                                   juce::jmax (v.attack + v.hold + v.decay + v.tailMs, v.pitchMs) * 1.15f);
        repaint();
    }
}

juce::Rectangle<float> KickEnvelopeEditor::plotArea() const
{
    return getLocalBounds().toFloat().reduced (8.0f, 6.0f).withTrimmedLeft (34.0f).withTrimmedBottom (14.0f);
}

float KickEnvelopeEditor::timeToX (float ms) const { auto r = plotArea(); return r.getX() + r.getWidth() * ms / viewMs; }
float KickEnvelopeEditor::xToTime (float x) const  { auto r = plotArea(); return (x - r.getX()) / r.getWidth() * viewMs; }
float KickEnvelopeEditor::ampToY (float a) const   { auto r = plotArea(); return r.getBottom() - a * r.getHeight(); }
float KickEnvelopeEditor::yToAmp (float y) const   { auto r = plotArea(); return (r.getBottom() - y) / r.getHeight(); }
float KickEnvelopeEditor::hzToY (float hz) const
{
    auto r = plotArea();
    const float n = std::log (juce::jlimit (kHzLo, kHzHi, hz) / kHzLo) / std::log (kHzHi / kHzLo);
    return r.getBottom() - n * r.getHeight();
}
float KickEnvelopeEditor::yToHz (float y) const
{
    auto r = plotArea();
    const float n = juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / r.getHeight());
    return kHzLo * std::pow (kHzHi / kHzLo, n);
}

juce::Point<float> KickEnvelopeEditor::handlePos (Handle h, const Values& v) const
{
    const float holdEnd = v.attack + v.hold, decayEnd = holdEnd + v.decay, tailEnd = decayEnd + v.tailMs;
    switch (h)
    {
        case HoldEnd:    return { timeToX (holdEnd), ampToY (1.0f) };
        case DecayEnd:   return { timeToX (decayEnd), ampToY (v.tailLevel) };
        case TailEnd:    return { timeToX (tailEnd), ampToY (0.0f) };
        case DecayCurve:
        {
            const float a = v.tailLevel + (1.0f - v.tailLevel) * std::pow (0.5f, v.decayCurve);
            return { timeToX (holdEnd + 0.5f * v.decay), ampToY (a) };
        }
        case PitchStart: return { timeToX (0.0f), hzToY (v.start) };
        case PitchEnd:   return { timeToX (v.pitchMs), hzToY (v.end) };
        case PitchCurve:
        {
            const float e = std::pow (0.5f, v.pitchCurve);
            return { timeToX (0.5f * v.pitchMs), hzToY (v.end * std::exp (std::log (v.start / v.end) * e)) };
        }
        case None: case NumHandles: break;
    }
    return {};
}

KickEnvelopeEditor::Handle KickEnvelopeEditor::findHandle (juce::Point<float> p) const
{
    Handle best = None;
    float bestD = 13.0f;
    for (int i = 0; i < NumHandles; ++i)
    {
        const float d = handlePos ((Handle) i, cached).getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = (Handle) i; }
    }
    return best;
}

void KickEnvelopeEditor::paint (juce::Graphics& g)
{
    const auto r = plotArea();
    const auto& v = cached;

    g.setColour (juce::Colour (0xc0030816));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Raster
    g.setFont (uiFont (11.0f));
    for (float hz : { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f })
    {
        const float y = hzToY (hz);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
        g.setColour (col::violet.withAlpha (0.85f));
        g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (r.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
    }
    const float step = viewMs > 1200.0f ? 250.0f : viewMs > 500.0f ? 100.0f : 50.0f;
    for (float t = 0.0f; t <= viewMs; t += step)
    {
        const float x = timeToX (t);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (col::textDim);
        g.drawText (juce::String ((int) t) + " ms", juce::Rectangle<float> (x - 30.0f, r.getBottom() + 1.0f, 60.0f, 13.0f),
                    juce::Justification::centred);
    }

    // Amplitude
    const float holdEnd = v.attack + v.hold, decayEnd = holdEnd + v.decay, tailEnd = decayEnd + v.tailMs;
    juce::Path amp;
    amp.startNewSubPath (timeToX (0.0f), ampToY (0.0f));
    const int steps = juce::jmax (64, (int) r.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const float t = viewMs * (float) i / (float) steps;
        float a;
        if (t < v.attack)        a = v.attack > 0.0f ? t / v.attack : 1.0f;
        else if (t < holdEnd)    a = 1.0f;
        else if (t < decayEnd)   a = v.tailLevel + (1.0f - v.tailLevel) * std::pow (1.0f - (t - holdEnd) / v.decay, v.decayCurve);
        else if (t < tailEnd)    { const float x = (t - decayEnd) / juce::jmax (0.01f, v.tailMs); a = v.tailLevel * (1.0f - x) * (1.0f - x); }
        else                     a = 0.0f;
        amp.lineTo (timeToX (t), ampToY (a));
    }
    juce::Path fill (amp);
    fill.lineTo (r.getRight(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (col::turq.withAlpha (0.45f), 0, r.getY(), col::blue.withAlpha (0.05f), 0, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (col::turq);
    g.strokePath (amp, juce::PathStrokeType (2.0f));

    // Pitch
    juce::Path pitch;
    for (int i = 0; i <= steps; ++i)
    {
        const float t = viewMs * (float) i / (float) steps;
        float hz = v.end;
        if (t < v.pitchMs)
            hz = v.end * std::exp (std::log (v.start / v.end) * std::pow (1.0f - t / v.pitchMs, v.pitchCurve));
        const auto p = juce::Point<float> (timeToX (t), hzToY (hz));
        if (i == 0) pitch.startNewSubPath (p); else pitch.lineTo (p);
    }
    g.setColour (col::magenta.withAlpha (0.3f));
    g.strokePath (pitch, juce::PathStrokeType (5.0f));
    g.setColour (col::violet.brighter (0.3f));
    g.strokePath (pitch, juce::PathStrokeType (2.0f));

    // Ziehpunkte
    for (int i = 0; i < NumHandles; ++i)
    {
        const auto p = handlePos ((Handle) i, v);
        const bool isPitch = i >= PitchStart;
        const bool active = (i == hover || i == dragging);
        const float rad = active ? 7.0f : 5.0f;
        g.setColour ((isPitch ? col::magenta : col::cyan).withAlpha (active ? 0.4f : 0.2f));
        g.fillEllipse (p.x - rad - 3.0f, p.y - rad - 3.0f, (rad + 3.0f) * 2.0f, (rad + 3.0f) * 2.0f);
        g.setGradientFill (chromeGradient ({ p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f }));
        g.fillEllipse (p.x - rad, p.y - rad, rad * 2.0f, rad * 2.0f);
    }

    // Anzeige
    g.setFont (uiFont (12.0f, true));
    auto info = getLocalBounds().reduced (44, 8).removeFromTop (16);
    juce::String txt;
    const auto noteName = [] (float hz) { return juce::MidiMessage::getMidiNoteName ((int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0)), true, true, 3); };
    switch (hover != None ? hover : dragging)
    {
        case HoldEnd:    txt = "Hold " + juce::String (v.hold, 1) + " ms"; break;
        case DecayEnd:   txt = "Decay " + juce::String ((int) v.decay) + T (" ms  ·  Tail-Pegel ") + juce::String (juce::roundToInt (v.tailLevel * 100)) + " %"; break;
        case TailEnd:    txt = "Tail " + juce::String ((int) v.tailMs) + " ms"; break;
        case DecayCurve: txt = "Decay-Kurve " + juce::String (v.decayCurve, 2); break;
        case PitchStart: txt = "Start " + juce::String ((int) v.start) + " Hz"; break;
        case PitchEnd:   txt = "Grundton " + juce::String (v.end, 1) + " Hz (" + noteName (v.end) + T (")  ·  Pitch-Zeit ") + juce::String (v.pitchMs, 1) + " ms"; break;
        case PitchCurve: txt = "Pitch-Kurve " + juce::String (v.pitchCurve, 2); break;
        case None: case NumHandles:
            txt = juce::String (v.start, 0) + " Hz > " + juce::String (v.end, 1) + " Hz (" + noteName (v.end) + T (")   Länge ")
                + juce::String ((int) tailEnd) + " ms";
            break;
    }
    // Grundton gegen die eingestellte Tonart prüfen (rot = passt nicht)
    const int key = (int) apvts.getRawParameterValue (pid::keyRoot)->load();
    const int scale = (int) apvts.getRawParameterValue (pid::keyScale)->load();
    const float tol = apvts.getRawParameterValue (pid::keyTol)->load();
    const auto ni = describeHz (v.end, key, scale);
    const bool bad = std::abs (ni.cents) > tol || (scale != kNumScales - 1 && ! ni.inKey);
    g.setColour (bad ? col::red : col::text);
    g.drawText (txt + (bad ? T ("  – nicht in Tonart!") : juce::String()), info, juce::Justification::centredRight);
}

void KickEnvelopeEditor::setParam (const char* id, float plain)
{
    if (auto* p = param (id))
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (plain)));
}

void KickEnvelopeEditor::beginGestures (Handle h)
{
    juce::StringArray ids;
    switch (h)
    {
        case HoldEnd:    ids = { pid::kHold }; break;
        case DecayEnd:   ids = { pid::kDecay, pid::kTailLevel }; break;
        case TailEnd:    ids = { pid::kTailMs }; break;
        case DecayCurve: ids = { pid::kDecayCurve }; break;
        case PitchStart: ids = { pid::kStart }; break;
        case PitchEnd:   ids = { pid::kPitchMs, pid::kEnd }; break;
        case PitchCurve: ids = { pid::kPitchCurve }; break;
        case None: case NumHandles: break;
    }
    for (auto& id : ids)
        if (auto* p = param (id.toRawUTF8())) p->beginChangeGesture();
    activeGestures = ids;
}

void KickEnvelopeEditor::endGestures()
{
    for (auto& id : activeGestures)
        if (auto* p = param (id.toRawUTF8())) p->endChangeGesture();
    activeGestures.clear();
}

void KickEnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto h = findHandle (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h == None ? juce::MouseCursor::NormalCursor : juce::MouseCursor::DraggingHandCursor);
}

void KickEnvelopeEditor::mouseExit (const juce::MouseEvent&) { hover = None; repaint(); }

void KickEnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = findHandle (e.position);
    dragStart = e.position;
    if (dragging == DecayCurve) dragStartCurve = cached.decayCurve;
    if (dragging == PitchCurve) dragStartCurve = cached.pitchCurve;
    if (dragging != None) beginGestures (dragging);
}

void KickEnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == None) return;
    const auto& v = cached;
    const float fine = e.mods.isShiftDown() ? 0.25f : 1.0f;
    const float t = xToTime (e.position.x);
    switch (dragging)
    {
        case HoldEnd:    setParam (pid::kHold, t - v.attack); break;
        case DecayEnd:   setParam (pid::kDecay, t - (v.attack + v.hold));
                         setParam (pid::kTailLevel, yToAmp (e.position.y)); break;
        case TailEnd:    setParam (pid::kTailMs, t - (v.attack + v.hold + v.decay)); break;
        case DecayCurve: setParam (pid::kDecayCurve, dragStartCurve * std::exp ((e.position.y - dragStart.y) * 0.015f * fine)); break;
        case PitchStart: setParam (pid::kStart, yToHz (e.position.y)); break;
        case PitchEnd:   setParam (pid::kPitchMs, t);
                         setParam (pid::kEnd, yToHz (e.position.y)); break;
        case PitchCurve: setParam (pid::kPitchCurve, dragStartCurve * std::exp ((e.position.y - dragStart.y) * 0.015f * fine)); break;
        case None: case NumHandles: break;
    }
}

void KickEnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    endGestures();
    dragging = None;
    timerCallback();
}

void KickEnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = findHandle (e.position);
    if (h == None) return;
    beginGestures (h);
    for (auto& id : activeGestures)
        if (auto* p = param (id.toRawUTF8())) p->setValueNotifyingHost (p->getDefaultValue());
    endGestures();
}

//==============================================================================
//==============================================================================
void SampleStrip::setSample (std::shared_ptr<const KickSample> s)
{
    smp = std::move (s);
    peaks.clear();
    if (smp != nullptr && ! smp->data.empty())
    {
        constexpr int bins = 400;
        peaks.assign (bins, 0.0f);
        const size_t n = smp->data.size();
        for (size_t i = 0; i < n; ++i)
        {
            const size_t b = std::min ((size_t) bins - 1, i * (size_t) bins / n);
            peaks[b] = std::max (peaks[b], std::abs (smp->data[i]));
        }
    }
    repaint();
}

void SampleStrip::setMarkers (float s, float l, float f, float t)
{
    auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
    if (same (s, startMs) && same (l, lengthMs) && same (f, fadeMs) && same (t, tuneSt)) return;
    startMs = s; lengthMs = l; fadeMs = f; tuneSt = t;
    repaint();
}

void SampleStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xc0020809));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::turq.withAlpha (0.35f));
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    if ((smp == nullptr || peaks.empty()) && compact)
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (10.5f));
        g.drawFittedText (T ("Kick-Sample hierher ziehen"), getLocalBounds().reduced (4, 2), juce::Justification::centred, 3);
        return;
    }
    if (smp == nullptr || peaks.empty())
    {
        g.setColour (col::textDim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (T ("Kein Sample geladen – WAV/AIFF/FLAC hierher ziehen oder LADEN.\n"
                             "Tipp: Top/Click-Sample + Synth-Sub, Low-Cut am Sample nutzen."),
                          getLocalBounds().reduced (8, 2), juce::Justification::centred, 2);
        return;
    }

    const double durMs = 1000.0 * (double) smp->data.size() / smp->rate;
    const double speed = std::pow (2.0, tuneSt / 12.0);                 // Tune verkürzt/verlängert
    auto msToX = [&] (double ms) { return r.getX() + (float) (ms / durMs) * r.getWidth(); };

    // Bereich, der tatsächlich klingt
    const double playStart = startMs;
    const double playEnd = std::min (durMs, startMs + (lengthMs + fadeMs) * speed);
    g.setColour (col::turq.withAlpha (0.12f));
    g.fillRect (juce::Rectangle<float>::leftTopRightBottom (msToX (playStart), r.getY(), msToX (playEnd), r.getBottom()));

    const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
    juce::Path wave;
    for (size_t i = 0; i < peaks.size(); ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) peaks.size();
        wave.addRectangle (x, mid - peaks[i] * half, juce::jmax (1.0f, r.getWidth() / (float) peaks.size()), peaks[i] * half * 2.0f);
    }
    g.setColour (col::cyan.withAlpha (0.85f));
    g.fillPath (wave);

    g.setColour (col::amber);
    g.drawVerticalLine ((int) msToX (playStart), r.getY(), r.getBottom());
    g.setColour (col::magenta.withAlpha (0.8f));
    g.drawVerticalLine ((int) msToX (std::min (durMs, startMs + lengthMs * speed)), r.getY(), r.getBottom());

    if (compact) return;
    g.setFont (uiFont (11.5f, true));
    juce::String info = juce::String::fromUTF8 (smp->name.c_str()) + "   " + juce::String (juce::roundToInt (durMs)) + " ms";
    if (smp->pitchHz > 0.0f)
        info << T ("   Grundton ≈ ") << juce::String (smp->pitchHz, 1) << " Hz"
             << (smp->pitchConfidence < 0.7f ? T (" (unsicher)") : juce::String());
    else
        info << T ("   Grundton: nicht erkannt");
    const auto tb = getLocalBounds().reduced (6, 2).removeFromTop (14);
    g.setColour (juce::Colour (0xc0020809));
    g.fillRoundedRectangle (tb.withWidth (juce::jmin (tb.getWidth(), juce::GlyphArrangement::getStringWidthInt (uiFont (11.5f, true), info) + 8)).toFloat(), 3.0f);
    g.setColour (col::text);
    g.drawText (info, tb.translated (3, 0), juce::Justification::centredLeft);
}

//==============================================================================
KickPanel::KickPanel (SamsiBasslineProcessor& p)
    : PanelFrame ("KICK", col::turq), proc (p), envelope (p.apvts),
      tabs (juce::StringArray { T ("HÜLLKURVE · CLICK"), T ("SAMPLE-LAYER"), T ("SAMPLE-MOD") }, col::turq),
      smpOn (p.apvts, pid::ksOn, "Sample an"),
      smpInvert (p.apvts, pid::ksInvert, "Invert"),
      modAmp (p.apvts, pid::ksAmpEnv, T ("Amp-Hüllk."), col::cyan),
      modPitch (p.apvts, pid::ksPitchEnv, T ("Pitch-Hüllk."), col::violet.brighter (0.3f)),
      modKey (p.apvts, pid::ksKey, T ("Grundton folgen")),
      modDrive (p.apvts, pid::ksDrive, T ("durch Sättigung")),
      sideOn (p.apvts, pid::ksOn, "An")
{
    sideStrip.setCompact (true);
    for (auto* comp : std::initializer_list<juce::Component*> { &sideStrip, &sideLoad, &sideClear, &sideAlign, &sideOn })
        addAndMakeVisible (comp);
    sideLoad.setTooltip (T ("Kick-Sample laden – oder Datei irgendwo auf den KICK-Bereich ziehen. Wird automatisch phasen-ausgerichtet."));
    sideClear.setTooltip ("Kick-Sample entfernen");
    sideAlign.setTooltip (T ("Sample erneut an der Synth-Kick ausrichten (Zeitversatz, Polarität; mit SAMPLE-MOD-Modulation)"));
    sideOn.get().setTooltip (T ("Sample-Layer ein/aus"));
    sideLoad.onClick = [this] { openChooser(); };
    sideClear.onClick = [this] { proc.clearKickSample(); message.clear(); update(); repaint(); };
    sideAlign.onClick = [this]
    {
        const auto r = proc.alignKickSampleNow();
        messageIsError = ! r.valid;
        message = r.valid ? T ("Ausgerichtet: ") + juce::String (r.coherenceBefore, 2) + T (" → ") + juce::String (r.coherenceAfter, 2)
                          : T ("Kein Sample geladen.");
        update(); repaint();
    };
    auto& apvts = p.apvts;
    addAndMakeVisible (envelope);
    const std::pair<const char*, const char*> defs[] = {
        { pid::kStart, "Start" }, { pid::kEnd, "Grundton" }, { pid::kPitchMs, "Pitch-Zeit" },
        { pid::kPitchCurve, "Pitch-Kurve" }, { pid::kPhase, "Startphase" }, { pid::kDrive, "Sättigung" },
        { pid::kAttack, "Attack" }, { pid::kHold, "Hold" }, { pid::kDecay, "Decay" },
        { pid::kDecayCurve, "Decay-Kurve" }, { pid::kTailLevel, "Tail-Pegel" }, { pid::kTailMs, "Tail" },
        { pid::kClickLevel, "Click" }, { pid::kClickDecay, "Click-Dauer" }, { pid::kClickTone, "Click-Ton" },
        { pid::kLevel, "Pegel" } };
    for (auto& d : defs)
    {
        const bool pitchish = juce::String (d.first).contains ("start_hz") || juce::String (d.first).contains ("end_hz")
                              || juce::String (d.first).contains ("pitch");
        knobs.push_back (std::make_unique<Knob> (apvts, d.first, d.second, pitchish ? col::violet.brighter (0.3f) : col::turq));
        addAndMakeVisible (*knobs.back());
    }

    const std::pair<const char*, const char*> sdefs[] = {
        { pid::ksLevel, "Pegel" }, { pid::ksTune, "Tune" }, { pid::ksOffset, "Verzög." }, { pid::ksStart, "Start" },
        { pid::ksLength, "Länge" }, { pid::ksFade, "Fade" }, { pid::ksLowCut, "Low-Cut" } };
    for (auto& d : sdefs)
    {
        smpKnobs.push_back (std::make_unique<Knob> (apvts, d.first, d.second, col::cyan));
        addChildComponent (*smpKnobs.back());
    }
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &clearButton, &alignButton, &smpOn, &smpInvert, &strip })
        addChildComponent (c);
    alignButton.setTooltip (T ("Richtet das Sample an der Synth-Kick aus: sucht Zeitversatz (±10 ms) und Polarität mit "
                               "maximaler Korrelation im Tiefband (< 200 Hz). Passiert automatisch beim Laden und bei Auto Fit."));
    smpInvert.get().setTooltip (T ("Polarität des Samples umkehren (wird von AUSRICHTEN gesetzt)."));
    alignButton.onClick = [this]
    {
        const auto r = proc.alignKickSampleNow();
        messageIsError = ! r.valid;
        message = r.valid ? T ("Ausgerichtet: Korrelation ") + juce::String (r.coherenceBefore, 2) + T (" → ")
                                + juce::String (r.coherenceAfter, 2) + (r.invert ? T (" · invertiert") : juce::String())
                          : T ("Kein Sample geladen.");
        update();
        repaint();
    };
    for (auto* comp : std::initializer_list<juce::Component*> { &modAmp, &modPitch, &modKey, &modDrive })
        addChildComponent (comp);
    modAmp.slider().setTooltip (T ("Wie stark die Lautstärke-Hüllkurve der Kick (Attack, Hold, Decay, Tail) das Sample formt. 0 % = Sample unverändert."));
    modPitch.slider().setTooltip (T ("Wie stark der Pitch-Verlauf der Kick (Start → Grundton, Pitch-Zeit, Pitch-Kurve) die Sample-Tonhöhe mitzieht."));
    modKey.get().setTooltip (T ("Sample-Grundton automatisch auf den Kick-Grundton stimmen (Grundton des Samples muss erkannt sein)."));
    modDrive.get().setTooltip (T ("Sample zusammen mit der Synth-Kick durch die Sättigung schicken (gemeinsamer Drive)."));
    loadButton.setTooltip (T ("Kick-Sample laden (WAV, AIFF, FLAC, OGG, MP3). Wird im Projekt gespeichert."));
    clearButton.setTooltip ("Sample entfernen");
    loadButton.onClick = [this] { openChooser(); };
    clearButton.onClick = [this] { proc.clearKickSample(); update(); };

    addAndMakeVisible (tabs);
    tabs.onChange = [this] (int page) { setPage (page); };
    setPage (0);
    update();
}

void KickPanel::setPage (int page)
{
    const bool smp = page == 1;
    for (size_t i = 6; i < knobs.size(); ++i) knobs[i]->setVisible (page == 0);
    for (auto* comp : std::initializer_list<juce::Component*> { &modAmp, &modPitch, &modKey, &modDrive })
        comp->setVisible (page == 2);
    for (auto& k : smpKnobs) k->setVisible (smp);
    for (auto* c : std::initializer_list<juce::Component*> { &loadButton, &clearButton, &alignButton, &smpOn, &smpInvert, &strip })
        c->setVisible (smp);
    repaint();
}

void KickPanel::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Kick-Sample laden", juce::File(), kAudioFilePatterns);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult()); });
}

void KickPanel::load (const juce::File& f)
{
    message = proc.loadKickSample (f);
    messageIsError = message.isNotEmpty();
    if (message.isEmpty())
    {
        if (auto* prm = proc.apvts.getParameter (pid::ksOn)) prm->setValueNotifyingHost (1.0f);
        tabs.setSelected (1);
        if (auto k = proc.getKickSample())
            message = T ("Geladen und ausgerichtet") + (k->pitchHz > 0.0f ? T (" · Grundton ") + juce::String (k->pitchHz, 1) + " Hz" : juce::String());
    }
    update();
    repaint();
}

void KickPanel::update()
{
    auto k = proc.getKickSample();
    if (k != shown)
    {
        shown = k;
        strip.setSample (k);
        sideStrip.setSample (k);
        repaint (sideBox);
    }
    const auto s = proc.readSettings().kick;
    strip.setMarkers (s.smpStartMs, s.smpLengthMs, s.smpFadeMs, s.smpTuneSt);
    sideStrip.setMarkers (s.smpStartMs, s.smpLengthMs, s.smpFadeMs, s.smpTuneSt);
    sideClear.setEnabled (k != nullptr);
    sideAlign.setEnabled (k != nullptr);
    clearButton.setEnabled (k != nullptr);
    alignButton.setEnabled (k != nullptr);
}

bool KickPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void KickPanel::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) { load (juce::File (f)); break; }
}

void KickPanel::resized()
{
    auto r = content();
    const int rowH = 72;
    auto bottom = r.removeFromBottom (12 + rowH + 22 + 168);
    // Sample-Fenster rechts neben der Hüllkurve
    sideBox = r.removeFromRight (150).reduced (0, 2);
    r.removeFromRight (6);
    envelope.setBounds (r.reduced (0, 2));
    {
        auto b = sideBox.reduced (6, 4);
        auto head = b.removeFromTop (18);
        sideClear.setBounds (head.removeFromRight (20));
        head.removeFromRight (4);
        sideOn.setBounds (head.removeFromRight (52));
        b.removeFromTop (3);
        auto foot = b.removeFromBottom (22);
        sideLoad.setBounds (foot.removeFromLeft (foot.getWidth() / 2 - 2));
        foot.removeFromLeft (4);
        sideAlign.setBounds (foot);
        b.removeFromBottom (3);
        sideInfo = b.removeFromBottom (26);
        sideStrip.setBounds (b);
    }

    labelPitch = bottom.removeFromTop (12);
    std::vector<juce::Component*> row1;
    for (int i = 0; i < 6; ++i) row1.push_back (knobs[(size_t) i].get());
    layoutGrid (bottom.removeFromTop (rowH), row1, 6, rowH);
    tabs.setBounds (bottom.removeFromTop (22));
    auto page = bottom;

    // Seite 0: Hüllkurve + Click
    {
        auto pg = page;
        labelAmp = pg.removeFromTop (12);
        std::vector<juce::Component*> a, b;
        for (int i = 6; i < 12; ++i) a.push_back (knobs[(size_t) i].get());
        for (int i = 12; i < 16; ++i) b.push_back (knobs[(size_t) i].get());
        layoutGrid (pg.removeFromTop (rowH), a, 6, rowH);
        labelClick = pg.removeFromTop (12);
        layoutGrid (pg.removeFromTop (rowH), b, 6, rowH);
    }
    // Seite 1: Sample-Layer
    {
        auto pg = page.withTrimmedTop (4);
        auto top = pg.removeFromTop (26);
        loadButton.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (4);
        clearButton.setBounds (top.removeFromLeft (24));
        top.removeFromLeft (6);
        smpOn.setBounds (top.removeFromLeft (128));
        top.removeFromLeft (6);
        alignButton.setBounds (top.removeFromLeft (104));
        top.removeFromLeft (6);
        smpInvert.setBounds (top);
        pg.removeFromTop (4);
        strip.setBounds (pg.removeFromTop (50));
        labelSample = pg.removeFromTop (12);
        std::vector<juce::Component*> k;
        for (auto& sk : smpKnobs) k.push_back (sk.get());
        layoutGrid (pg.removeFromTop (rowH), k, 7, rowH);
    }
    // Seite 2: Sample-Modulation durch die Kick-Regler
    {
        auto pg = page.withTrimmedTop (4);
        labelMod = pg.removeFromTop (12);
        auto row = pg.removeFromTop (rowH);
        const int kw = row.getWidth() / 6;
        modAmp.setBounds (row.removeFromLeft (kw));
        modPitch.setBounds (row.removeFromLeft (kw));
        row.removeFromLeft (6);
        auto tg = row.removeFromLeft (kw * 2 + 10).withSizeKeepingCentre (kw * 2 + 10, 58);
        modKey.setBounds (tg.removeFromTop (28).reduced (0, 2));
        modDrive.setBounds (tg.removeFromTop (28).reduced (0, 2));
        modHelp = pg.reduced (2, 4);
    }
}

void KickPanel::paintOverChildren (juce::Graphics& g)
{
    // Sample-Fenster: Rahmen, Titel, Grundton/Ausrichtung
    {
        const auto box = sideBox.toFloat();
        g.setColour (col::turq.withAlpha (0.35f));
        g.drawRoundedRectangle (box, 7.0f, 1.0f);
        g.setFont (uiFont (11.0f, true));
        g.setColour (col::turq);
        g.drawText ("SAMPLE", sideBox.reduced (8, 4).removeFromTop (18), juce::Justification::centredLeft);
        auto k = proc.getKickSample();
        g.setFont (uiFont (10.0f));
        g.setColour (col::textDim);
        juce::String l1, l2;
        if (k != nullptr)
        {
            const auto s = proc.readSettings().kick;
            l1 = juce::String::fromUTF8 (k->name.c_str());
            l2 = (k->pitchHz > 0.0f ? juce::String (k->pitchHz, 1) + " Hz" : T ("Grundton ?"))
                 + T (" · ") + juce::String (s.smpOffsetMs + s.smpStartMs, 2) + " ms" + (s.smpInvert ? T (" · inv") : juce::String());
        }
        else
            l1 = T ("auch per Drag & Drop");
        auto info = sideInfo;
        g.drawFittedText (l1, info.removeFromTop (13), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (col::text);
        g.drawFittedText (l2, info, juce::Justification::centredLeft, 1, 0.8f);
    }

    drawSectionLabel (g, labelPitch, "PITCH · PHASE · SÄTTIGUNG", col::violet.brighter (0.4f));
    if (tabs.getSelected() == 0)
    {
        drawSectionLabel (g, labelAmp, "LAUTSTÄRKE-HÜLLKURVE", col::turq);
        drawSectionLabel (g, labelClick, "CLICK-LAYER · AUSGANG", col::cyan);
    }
    else if (tabs.getSelected() == 2)
    {
        drawSectionLabel (g, labelMod, T ("KICK-REGLER MODULIEREN DAS SAMPLE"), col::cyan);
        g.setColour (col::textDim);
        g.setFont (uiFont (11.0f));
        g.drawFittedText (T ("Die Hüllkurven oben (Pitch und Lautstärke) wirken anteilig auf das Sample. Nach Änderungen "
                             "AUSRICHTEN (Seite SAMPLE-LAYER) oder Auto Fit drücken – die Ausrichtung rendert das modulierte Sample."),
                          modHelp, juce::Justification::topLeft, 4);
    }
    else
    {
        drawSectionLabel (g, labelSample, "SAMPLE: PEGEL · STIMMUNG · TIMING · HÜLLKURVE · HOCHPASS", col::cyan);
        if (message.isNotEmpty())
        {
            g.setColour (messageIsError ? col::red : col::cyan);
            g.setFont (uiFont (11.5f, true));
            g.drawText (message, strip.getBounds().translated (0, -2), juce::Justification::bottomRight);
        }
    }
    if (dragOver)
    {
        g.setColour (col::cyan.withAlpha (0.15f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f), 10.0f);
        g.setColour (col::cyan);
        g.setFont (uiFont (18.0f, true));
        g.drawText ("Sample hier ablegen -> Kick-Sample-Layer", getLocalBounds(), juce::Justification::centred);
    }
}

} // namespace samsi::ui
