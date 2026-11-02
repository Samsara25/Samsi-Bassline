#include "FlowerOrb.h"

namespace samsi::ui
{

namespace
{
    constexpr float kPi = juce::MathConstants<float>::pi;

    /** Blütenblatt = Schnittfläche zweier gleich großer Kreise (Radius r) mit Mittelpunktabstand d.
        Für d = sqrt(3) r entsteht das klassische Blatt der Blume des Lebens (Spitzenwinkel 60°). */
    juce::Path lens (juce::Point<float> a, juce::Point<float> b, float r)
    {
        juce::Path p;
        const float d = a.getDistanceFrom (b);
        const float half = std::acos (juce::jlimit (-1.0f, 1.0f, d / (2.0f * r)));   // halber Öffnungswinkel je Bogen
        const float phi = std::atan2 (b.y - a.y, b.x - a.x);
        constexpr int n = 24;
        for (int i = 0; i <= n; ++i)
        {
            const float t = phi - half + 2.0f * half * (float) i / (float) n;
            const juce::Point<float> q (a.x + r * std::cos (t), a.y + r * std::sin (t));
            if (i == 0) p.startNewSubPath (q); else p.lineTo (q);
        }
        for (int i = 1; i < n; ++i)
        {
            const float t = phi + kPi - half + 2.0f * half * (float) i / (float) n;
            p.lineTo (b.x + r * std::cos (t), b.y + r * std::sin (t));
        }
        p.closeSubPath();
        return p;
    }
}

FlowerOrb::FlowerOrb()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (T ("AUTO FIT – klicken (beliebig oft).\n"
                   "Rendert das Pattern, misst Kick, Bass und Summe und sucht Bass-Startphase, Polarität, "
                   "Hüllkurve, Ducking, Versatz (wenn erlaubt) und Pegel. Ein geladenes Kick-Sample wird dabei "
                   "automatisch zur Synth-Kick phasenausgerichtet.\n"
                   "Die Blume pulsiert, wenn die schlimmste Auslöschung im Tiefband (20–60 Hz) unter 3 dB liegt "
                   "und Kick und Bass in der Tonart zusammenpassen."));
}

juce::Point<float> FlowerOrb::orbCentre() const
{
    return { (float) getWidth() * 0.5f, ((float) getHeight() - 70.0f) * 0.5f + 4.0f };
}

float FlowerOrb::orbRadius() const
{
    return juce::jmax (10.0f, juce::jmin ((float) getWidth(), (float) getHeight() - 70.0f) * 0.5f * 0.80f);
}

bool FlowerOrb::hitTest (int x, int y)
{
    return orbCentre().getDistanceFrom ({ (float) x, (float) y }) <= orbRadius() * 1.12f;
}

void FlowerOrb::mouseDown (const juce::MouseEvent&) { pressed = true; repaint(); }

void FlowerOrb::mouseUp (const juce::MouseEvent& e)
{
    const bool inside = pressed && hitTest (e.x, e.y);
    pressed = false;
    repaint();
    if (inside && ! state.running && onClick) onClick();
}

void FlowerOrb::setState (const State& s)
{
    const bool changed = s.running != state.running || s.aligned != state.aligned || s.known != state.known
                         || s.line1 != state.line1 || s.line2 != state.line2 || std::abs (s.progress - state.progress) > 0.002f;
    state = s;
    if (changed) repaint();
}

void FlowerOrb::advance (float kickLevel)
{
    breath += 0.035f;
    if (breath > 2.0f * kPi) breath -= 2.0f * kPi;
    spin += state.running ? 0.05f : 0.004f;
    if (spin > 2.0f * kPi) spin -= 2.0f * kPi;

    // Puls nur, wenn die Messung im Ziel liegt – dann im Takt der Kick
    const float target = state.aligned ? juce::jlimit (0.0f, 1.0f, kickLevel * 1.5f) : 0.0f;
    pulse = juce::jmax (pulse * 0.82f, target);
    glow += ((state.aligned ? 1.0f : 0.0f) - glow) * 0.08f;
    repaint();
}

void FlowerOrb::drawFlower (juce::Graphics& g, juce::Point<float> c, float R)
{
    // --- Kugel (Mint/Türkis, passend zur Logo-Palette)
    {
        juce::ColourGradient sphere (juce::Colour (0xffa6f7e6), c.x - R * 0.38f, c.y - R * 0.42f,
                                     juce::Colour (0xff03302f), c.x + R * 0.55f, c.y + R * 0.7f, true);
        sphere.addColour (0.30, juce::Colour (0xff4fd6c3));
        sphere.addColour (0.68, juce::Colour (0xff138a83));
        g.setGradientFill (sphere);
        g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    }

    // --- Gitter: 37 Kreise (Ringe 0..3), sichtbar innerhalb von 3r
    const float r = R / 3.55f;
    const float boundary = 3.0f * r;
    std::vector<juce::Point<float>> centres;
    for (int q = -3; q <= 3; ++q)
        for (int s = -3; s <= 3; ++s)
            if (std::abs (q + s) <= 3)
                centres.push_back ({ c.x + r * ((float) q + 0.5f * (float) s), c.y + r * (float) s * 0.8660254f });

    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip;
    clip.addEllipse (c.x - boundary, c.y - boundary, 2.0f * boundary, 2.0f * boundary);
    g.reduceClipRegion (clip);

    // Blütenblätter = Linsen von Kreisen im Abstand sqrt(3) r (übernächste Nachbarn)
    juce::Path petals;
    for (size_t i = 0; i < centres.size(); ++i)
        for (size_t j = i + 1; j < centres.size(); ++j)
            if (std::abs (centres[i].getDistanceFrom (centres[j]) - 1.7320508f * r) < r * 0.01f)
            {
                const auto mid = (centres[i] + centres[j]) * 0.5f;
                if (mid.getDistanceFrom (c) < boundary + r * 0.5f)
                    petals.addPath (lens (centres[i], centres[j], r));
            }
    {
        juce::ColourGradient pg (juce::Colour (0xfffbfffe), c.x - R * 0.5f, c.y - R * 0.6f,
                                 juce::Colour (0xffc4f5ec), c.x + R * 0.6f, c.y + R * 0.7f, false);
        g.setGradientFill (pg);
        g.setOpacity (0.86f);
        g.fillPath (petals);
        g.setOpacity (1.0f);
    }
    // Kreislinien
    g.setColour (juce::Colour (0xa0074e49));
    for (auto& p : centres)
    {
        juce::Path circle;
        circle.addEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
        g.strokePath (circle, juce::PathStrokeType (juce::jmax (0.6f, R * 0.006f)));
    }
    g.restoreState();
    g.saveState();

    // Doppelter Grenzring
    for (float k : { 1.0f, 1.045f })
    {
        juce::Path ring;
        ring.addEllipse (c.x - boundary * k, c.y - boundary * k, 2.0f * boundary * k, 2.0f * boundary * k);
        g.setColour (juce::Colour (0xccf4fffc));
        g.strokePath (ring, juce::PathStrokeType (juce::jmax (0.8f, R * 0.012f)));
    }

    // --- Kugelschattierung + Glanzlicht (3D-Wirkung)
    {
        juce::ColourGradient shade (juce::Colour (0x00000000), c.x - R * 0.3f, c.y - R * 0.35f,
                                    juce::Colour (0x8c021e20), c.x + R * 0.75f, c.y + R * 0.8f, true);
        shade.addColour (0.55, juce::Colour (0x10031c1d));
        g.setGradientFill (shade);
        g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);

        juce::ColourGradient spec (juce::Colour (0x48ffffff), c.x - R * 0.42f, c.y - R * 0.5f,
                                   juce::Colour (0x00ffffff), c.x - R * 0.42f + R * 0.55f, c.y - R * 0.5f, true);
        g.setGradientFill (spec);
        g.fillEllipse (c.x - R * 0.85f, c.y - R * 0.88f, R * 0.95f, R * 0.75f);
    }
    // Chromrand
    juce::Path rim;
    rim.addEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
    g.setGradientFill (chromeGradient ({ c.x - R, c.y - R, 2.0f * R, 2.0f * R }));
    g.strokePath (rim, juce::PathStrokeType (juce::jmax (1.2f, R * 0.022f)));
}

void FlowerOrb::paint (juce::Graphics& g)
{
    const auto c = orbCentre();
    const float R = orbRadius();
    const float scale = 1.0f + 0.045f * pulse + (pressed ? -0.025f : 0.0f) + (hover ? 0.01f : 0.0f);

    // --- Halo (stärker, wenn ausgerichtet)
    const float haloA = 0.10f + 0.10f * (0.5f + 0.5f * std::sin (breath)) + 0.35f * glow * (0.35f + 0.65f * pulse);
    {
        const auto haloCol = state.aligned ? col::cyan : (state.known ? col::amber : col::ice);
        juce::ColourGradient halo (haloCol.withAlpha (haloA), c.x, c.y, haloCol.withAlpha (0.0f), c.x + R * 1.55f, c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (c.x - R * 1.55f, c.y - R * 1.55f, R * 3.1f, R * 3.1f);
    }

    // --- äußere Ringe: drehender Blütenkranz + Punktring
    sym::petalRing (g, c, R * 1.13f, 24, R * 0.07f, spin, col::cyan.withAlpha (0.35f + 0.5f * glow * pulse), 0.2f + 0.6f * pulse);
    sym::dottedRing (g, c, R * 1.24f, 72, 1.8f, col::ice.withAlpha (0.45f));

    // --- Fortschritt / Status-Bogen
    {
        juce::Path track;
        track.addCentredArc (c.x, c.y, R * 1.06f, R * 1.06f, 0.0f, 0.0f, 2.0f * kPi, true);
        g.setColour (juce::Colour (0x40a8fff4));
        g.strokePath (track, juce::PathStrokeType (2.0f));
        if (state.running)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 1.06f, R * 1.06f, 0.0f, 0.0f, 2.0f * kPi * juce::jlimit (0.01f, 1.0f, state.progress), true);
            sym::glowStroke (g, arc, col::cyan, 4.0f, 1.0f);
            // umlaufender Komet
            const float a = spin * 3.0f;
            g.setColour (col::chromeHi);
            g.fillEllipse (c.x + std::sin (a) * R * 1.06f - 4.0f, c.y - std::cos (a) * R * 1.06f - 4.0f, 8.0f, 8.0f);
        }
        else if (state.known)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, R * 1.06f, R * 1.06f, 0.0f, 0.0f, 2.0f * kPi, true);
            sym::glowStroke (g, arc, state.aligned ? col::green : col::amber, 2.0f, 0.4f + 0.6f * glow);
        }
    }

    // --- Blume (zwischengespeichert, in Kugelgröße)
    const int sz = (int) std::ceil (R * 2.0f) + 4;
    if (cache.isNull() || cache.getWidth() != sz)
    {
        cache = juce::Image (juce::Image::ARGB, sz, sz, true);
        juce::Graphics cg (cache);
        cg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        drawFlower (cg, { (float) sz * 0.5f, (float) sz * 0.5f }, R);
    }
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::scale (scale, scale, c.x, c.y));
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImageAt (cache, (int) std::round (c.x - (float) sz * 0.5f), (int) std::round (c.y - (float) sz * 0.5f));
        if (! state.aligned)
        {
            // gedämpft, solange die Messung nicht im Ziel liegt
            g.setColour (col::bgDeep.withAlpha (state.running ? 0.15f : 0.28f));
            g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
        }
        else if (pulse > 0.01f)
        {
            // Lichtimpuls auf der Kugel
            juce::ColourGradient flash (juce::Colours::white.withAlpha (0.28f * pulse), c.x, c.y,
                                        col::cyan.withAlpha (0.0f), c.x + R, c.y, true);
            g.setGradientFill (flash);
            g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
        }
        if (hover && ! state.running)
        {
            juce::Path rim;
            rim.addEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);
            sym::glowStroke (g, rim, col::cyan, 2.0f, 1.0f);
        }
    }

    // --- Beschriftung: AUTO FIT (Chrom) + Status
    auto text = getLocalBounds().removeFromBottom (68);
    {
        juce::GlyphArrangement ga;
        const juce::String title = state.running ? "ANALYSIERE" : "AUTO FIT";
        const auto f = uiFont (26.0f, true);
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (f, title);
        ga.addLineOfText (f, title, c.x - tw * 0.5f, (float) text.getY() + 22.0f);
        juce::Path tp;
        ga.createPath (tp);
        g.setColour (col::cyan.withAlpha (0.25f + 0.35f * glow));
        g.strokePath (tp, juce::PathStrokeType (4.0f));
        g.setGradientFill (chromeGradient (tp.getBounds()));
        g.fillPath (tp);
    }
    text.removeFromTop (28);
    g.setFont (uiFont (12.5f, true));
    g.setColour (state.running ? col::cyan : state.aligned ? col::green : state.known ? col::amber : col::textDim);
    g.drawText (state.line1, text.removeFromTop (18), juce::Justification::centred);
    g.setFont (uiFont (11.5f));
    g.setColour (col::textDim);
    g.drawText (state.line2, text.removeFromTop (16), juce::Justification::centred);
}

} // namespace samsi::ui
