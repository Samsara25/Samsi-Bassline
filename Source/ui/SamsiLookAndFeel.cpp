#include "SamsiLookAndFeel.h"

namespace samsi::ui
{

juce::Font uiFont (float size, bool bold)
{
    auto opts = juce::FontOptions (size);
    if (bold) opts = opts.withStyle ("Bold");
    return juce::Font (opts);
}

juce::ColourGradient chromeGradient (juce::Rectangle<float> r, bool vertical)
{
    juce::ColourGradient g (col::chromeHi, r.getX(), r.getY(),
                            col::chromeLo, vertical ? r.getX() : r.getRight(), vertical ? r.getBottom() : r.getY(), false);
    g.addColour (0.18, juce::Colour (0xffb9c6dc));
    g.addColour (0.45, juce::Colour (0xff4a5874));
    g.addColour (0.55, juce::Colour (0xffd7e2f4));
    g.addColour (0.80, juce::Colour (0xff8b9ab6));
    return g;
}

//==============================================================================
SamsiLookAndFeel::SamsiLookAndFeel()
{
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::ComboBox::backgroundColourId, col::bgDeep);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::turq.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId, col::turq);
    setColour (juce::PopupMenu::backgroundColourId, col::bgMid);
    setColour (juce::PopupMenu::textColourId, col::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::blue.withAlpha (0.6f));
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, col::bgDeep);
    setColour (juce::ToggleButton::textColourId, col::text);
    setColour (juce::Slider::textBoxTextColourId, col::text);
    setColour (juce::TooltipWindow::backgroundColourId, col::bgMid);
    setColour (juce::TooltipWindow::textColourId, col::text);
    setColour (juce::TooltipWindow::outlineColourId, col::turq);
}

void SamsiLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto r = area.withSizeKeepingCentre (size, size);
    const auto c = r.getCentre();
    const float radius = size * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    // Punktring wie im Logo: leuchtet bis zum aktuellen Wert
    {
        constexpr int dots = 21;
        for (int i = 0; i < dots; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (dots - 1);
            const bool lit = (float) i / (float) (dots - 1) <= pos + 1.0e-3f;
            const float dr = radius + 1.5f;
            g.setColour (lit ? accent.withAlpha (0.85f) : col::gunmetal.withAlpha (0.5f));
            g.fillEllipse (c.x + std::sin (a) * dr - 1.0f, c.y - std::cos (a) * dr - 1.0f, 2.0f, 2.0f);
        }
    }

    // Spur
    juce::Path track;
    track.addCentredArc (c.x, c.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff10262a));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Wertbogen mit Glühen
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius - 3.0f, radius - 3.0f, 0.0f, startAngle, angle, true);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (arc, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Chrom-Knopf
    const auto knob = r.reduced (size * 0.2f);
    g.setGradientFill (chromeGradient (knob));
    g.fillEllipse (knob);
    const auto cap = knob.reduced (knob.getWidth() * 0.12f);
    juce::ColourGradient capG (juce::Colour (0xff15343a), cap.getX(), cap.getY(), col::bgDeep, cap.getRight(), cap.getBottom(), false);
    g.setGradientFill (capG);
    g.fillEllipse (cap);

    // Zeiger
    juce::Path p;
    const float pl = cap.getWidth() * 0.5f;
    p.addRoundedRectangle (-1.6f, -pl, 3.2f, pl * 0.62f, 1.4f);
    g.setColour (col::cyan);
    g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void SamsiLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                         juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const auto track = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
    g.setColour (juce::Colour (0xff17243f));
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (s.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (track.withRight (pos), 2.0f);
    const auto thumb = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, r.getCentreY() });
    g.setGradientFill (chromeGradient (thumb));
    g.fillEllipse (thumb);
}

void SamsiLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    const auto led = r.withWidth (r.getHeight()).reduced (r.getHeight() * 0.22f);

    g.setColour (juce::Colour (0xff0b1530));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour ((on ? col::turq : col::chromeLo).withAlpha (highlighted ? 1.0f : 0.8f));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);

    if (on)
    {
        g.setColour (col::turq.withAlpha (0.35f));
        g.fillEllipse (led.expanded (3.0f));
    }
    g.setColour (on ? col::cyan : juce::Colour (0xff2a3858));
    g.fillEllipse (led);

    g.setColour (on ? col::text : col::textDim);
    g.setFont (uiFont (juce::jmin (13.0f, r.getHeight() * 0.62f), on));
    g.drawFittedText (b.getButtonText(), r.withTrimmedLeft (r.getHeight()).toNearestInt().reduced (2, 0),
                      juce::Justification::centredLeft, 1);
}

void SamsiLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    if (on)
    {
        juce::ColourGradient grad (col::cyan, r.getX(), r.getY(), col::turq.darker (0.3f), r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
    }
    else
        g.setGradientFill (chromeGradient (r));
    g.fillRoundedRectangle (r, 6.0f);

    if (! on)
    {
        auto inner = r.reduced (2.0f);
        juce::ColourGradient ig (juce::Colour (0xff1c2c52), inner.getX(), inner.getY(), col::bgDeep, inner.getX(), inner.getBottom(), false);
        g.setGradientFill (ig);
        g.fillRoundedRectangle (inner, 5.0f);
    }
    if (highlighted || down)
    {
        g.setColour (col::turq.withAlpha (down ? 0.35f : 0.18f));
        g.fillRoundedRectangle (r, 6.0f);
    }
}

void SamsiLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (uiFont (juce::jmin (15.0f, (float) b.getHeight() * 0.5f), true));
    g.setColour (b.getToggleState() ? col::bgDeep : (b.isEnabled() ? col::text : col::textDim));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

void SamsiLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (col::bgDeep);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.2f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (col::turq);
    g.fillPath (arrow);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const Text& label, juce::Colour accent)
{
    param = apvts.getParameter (paramId);
    jassert (param != nullptr);

    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.setColour (juce::Slider::rotarySliderFillColourId, accent);
    knob.setPopupDisplayEnabled (false, false, nullptr);
    knob.setDoubleClickReturnValue (true, param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
    knob.setTooltip (param != nullptr ? param->getName (64) + "  (Doppelklick = Standard, Shift = fein)" : juce::String());
    knob.setVelocityModeParameters (1.0, 1, 0.08, true, juce::ModifierKeys::shiftModifier);
    addAndMakeVisible (knob);

    name.setText (label.s, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (uiFont (12.5f, true));
    name.setColour (juce::Label::textColourId, col::textDim);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    value.setJustificationType (juce::Justification::centred);
    value.setFont (uiFont (12.5f));
    value.setColour (juce::Label::textColourId, col::text);
    value.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (value);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, paramId, knob);
    knob.onValueChange = [this] { updateValueText(); };
    updateValueText();
}

void Knob::updateValueText()
{
    if (param != nullptr)
        value.setText (param->getCurrentValueAsText(), juce::dontSendNotification);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (15));
    value.setBounds (r.removeFromBottom (15));
    knob.setBounds (r);
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const Text& label)
{
    button.setButtonText (label.s);
    if (auto* p = apvts.getParameter (paramId)) button.setTooltip (p->getName (64));
    addAndMakeVisible (button);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, paramId, button);
}

Choice::Choice (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const Text& label)
{
    name.setText (label.s, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (uiFont (12.5f, true));
    name.setColour (juce::Label::textColourId, col::textDim);
    addAndMakeVisible (name);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (paramId)))
        box.addItemList (p->choices, 1);
    addAndMakeVisible (box);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, paramId, box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    if (labelLeft > 0)
    {
        name.setJustificationType (juce::Justification::centredRight);
        name.setBounds (r.removeFromLeft (labelLeft));
        r.removeFromLeft (6);
        box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (26, r.getHeight())));
        return;
    }
    name.setBounds (r.removeFromTop (15));
    box.setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (26, r.getHeight())));
}

//==============================================================================
PanelFrame::PanelFrame (const Text& t, juce::Colour a) : title (t.s), accent (a)
{
    setInterceptsMouseClicks (false, true);
}

void PanelFrame::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);

    // Glas: schwarz-petrol wie das Logo
    juce::ColourGradient glass (juce::Colour (0xe8081a1e), r.getX(), r.getY(),
                                juce::Colour (0xf0020607), r.getX(), r.getBottom(), false);
    g.setGradientFill (glass);
    g.fillRoundedRectangle (r, 12.0f);

    // Doppelring wie im Logo: Chrom außen, leuchtende Linie innen
    g.setGradientFill (chromeGradient (r));
    g.drawRoundedRectangle (r, 12.0f, 2.0f);
    {
        juce::Path inner;
        inner.addRoundedRectangle (r.reduced (4.0f), 9.0f);
        sym::glowStroke (g, inner, accent.withAlpha (0.55f), 0.9f, 0.6f);
    }
    // Eck-Ornamente: kleine Tropfen
    for (auto corner : { r.getTopRight().translated (-12.0f, 12.0f), r.getBottomLeft().translated (12.0f, -12.0f),
                         r.getBottomRight().translated (-12.0f, -12.0f) })
        g.setColour (accent.withAlpha (0.6f)), g.fillPath (sym::drop (corner, 7.0f, juce::MathConstants<float>::pi * 0.25f));

    // Titel mit Logo-Zeichen
    auto tr = getLocalBounds().reduced (14, 6).removeFromTop (22);
    const auto icon = juce::Rectangle<float> ((float) tr.getX(), (float) tr.getY(), 20.0f, 20.0f);
    sym::glowStroke (g, sym::lotusDrop (icon), col::cyan, 1.2f, 0.8f);
    g.setFont (uiFont (17.0f, true));
    juce::GlyphArrangement ga;
    ga.addLineOfText (g.getCurrentFont(), title, (float) tr.getX() + 26.0f, (float) tr.getBottom() - 5.0f);
    juce::Path tp;
    ga.createPath (tp);
    const auto tb = tp.getBounds();
    g.setColour (col::cyan.withAlpha (0.25f));
    g.strokePath (tp, juce::PathStrokeType (3.0f));
    g.setGradientFill (chromeGradient (tb));
    g.fillPath (tp);

    // Linie mit Punkten bis zum Rand
    const float x0 = tb.getRight() + 10.0f, x1 = (float) tr.getRight() - 14.0f, cy = tb.getCentreY();
    if (x1 > x0)
    {
        g.setColour (accent.withAlpha (0.8f));
        g.fillRoundedRectangle (x0, cy - 1.0f, (x1 - x0) * 0.55f, 2.0f, 1.0f);
        for (float x = x0 + (x1 - x0) * 0.6f; x < x1; x += 7.0f)
            g.fillEllipse (x - 1.2f, cy - 1.2f, 2.4f, 2.4f);
        g.fillPath (sym::drop ({ x1 + 6.0f, cy }, 9.0f, juce::MathConstants<float>::halfPi));
    }
}

void PanelFrame::drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const Text& textIn, juce::Colour c)
{
    const juce::String& text = textIn.s;
    g.setColour (c.withAlpha (0.9f));
    g.setFont (uiFont (11.5f, true));
    g.drawText (text, r, juce::Justification::centredLeft);
    g.setColour (c.withAlpha (0.25f));
    const int tw = juce::GlyphArrangement::getStringWidthInt (uiFont (11.5f, true), text);
    g.fillRect (r.getX() + tw + 6, r.getCentreY(), juce::jmax (0, r.getWidth() - tw - 6), 1);
}

void layoutGrid (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int columns, int rowHeight)
{
    if (columns <= 0) return;
    const int cw = area.getWidth() / columns;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const int row = (int) i / columns, c = (int) i % columns;
        if (items[i] != nullptr)
            items[i]->setBounds (area.getX() + c * cw, area.getY() + row * rowHeight, cw, rowHeight);
    }
}

//==============================================================================
TabStrip::TabStrip (const juce::StringArray& n, juce::Colour a) : names (n), accent (a) {}

int TabStrip::indexAt (juce::Point<float> p) const
{
    if (names.isEmpty()) return -1;
    const float w = (float) getWidth() / (float) names.size();
    return juce::jlimit (0, names.size() - 1, (int) (p.x / w));
}

void TabStrip::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float w = r.getWidth() / (float) names.size();
    g.setColour (accent.withAlpha (0.25f));
    g.fillRect (r.withTop (r.getBottom() - 1.0f));
    for (int i = 0; i < names.size(); ++i)
    {
        auto cell = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight());
        const bool sel = i == selected, hov = i == hover;
        if (sel)
        {
            g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.0f), cell.getX(), cell.getY(),
                                                     accent.withAlpha (0.22f), cell.getX(), cell.getBottom(), false));
            g.fillRect (cell);
            g.setColour (accent);
            g.fillRect (cell.withTop (cell.getBottom() - 2.0f).reduced (6.0f, 0.0f));
            g.fillPath (sym::drop ({ cell.getX() + 10.0f, cell.getCentreY() }, 7.0f));
        }
        g.setFont (uiFont (12.0f, true));
        g.setColour (sel ? col::cyan : (hov ? col::text : col::textDim));
        g.drawText (names[i], cell.withTrimmedLeft (sel ? 14.0f : 0.0f), juce::Justification::centred);
    }
}

void TabStrip::mouseDown (const juce::MouseEvent& e) { setSelected (indexAt (e.position)); }
void TabStrip::mouseMove (const juce::MouseEvent& e) { const int h = indexAt (e.position); if (h != hover) { hover = h; repaint(); } }

void TabStrip::setSelected (int index, bool notify)
{
    if (index < 0 || index >= names.size()) return;
    const bool changed = index != selected;
    selected = index;
    repaint();
    if (notify && changed && onChange) onChange (index);
}

//==============================================================================
namespace sym
{
    juce::Path drop (juce::Point<float> c, float size, float angle)
    {
        juce::Path p;
        const float r = size * 0.42f;
        p.startNewSubPath (0.0f, -size * 0.62f);
        p.cubicTo (r * 0.55f, -size * 0.2f, r, 0.0f, r, size * 0.12f);
        p.addCentredArc (0.0f, size * 0.12f, r, r, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, false);
        p.cubicTo (-r, 0.0f, -r * 0.55f, -size * 0.2f, 0.0f, -size * 0.62f);
        p.closeSubPath();
        p.applyTransform (juce::AffineTransform::rotation (angle).translated (c));
        return p;
    }

    juce::Path lotusDrop (juce::Rectangle<float> a)
    {
        juce::Path p;
        const auto c = a.getCentre();
        const float s = juce::jmin (a.getWidth(), a.getHeight());
        juce::Path outer = drop ({ c.x, c.y - s * 0.12f }, s * 0.55f);
        p.addPath (outer);
        p.addEllipse (c.x - s * 0.07f, c.y - s * 0.07f, s * 0.14f, s * 0.14f);
        juce::Path moon;
        moon.addCentredArc (c.x, c.y + s * 0.12f, s * 0.36f, s * 0.22f, 0.0f,
                            juce::MathConstants<float>::halfPi * 1.25f, juce::MathConstants<float>::halfPi * 2.75f, true);
        p.addPath (juce::Path (moon));
        return p;
    }

    void petalRing (juce::Graphics& g, juce::Point<float> c, float radius, int count, float petalSize,
                    float rotation, juce::Colour colour, float glow)
    {
        for (int i = 0; i < count; ++i)
        {
            const float a = rotation + juce::MathConstants<float>::twoPi * (float) i / (float) count;
            const juce::Point<float> pc (c.x + std::sin (a) * radius, c.y - std::cos (a) * radius);
            auto p = drop (pc, petalSize, a);
            glowStroke (g, p, colour, 1.3f, glow);
            g.setColour (colour.withAlpha (0.9f));
            g.fillEllipse (pc.x - petalSize * 0.08f, pc.y - petalSize * 0.02f, petalSize * 0.16f, petalSize * 0.16f);
        }
    }

    void dottedRing (juce::Graphics& g, juce::Point<float> c, float radius, int count, float dotSize, juce::Colour colour)
    {
        g.setColour (colour);
        for (int i = 0; i < count; ++i)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) i / (float) count;
            g.fillEllipse (c.x + std::sin (a) * radius - dotSize * 0.5f, c.y - std::cos (a) * radius - dotSize * 0.5f, dotSize, dotSize);
        }
    }

    void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow)
    {
        if (glow > 0.0f)
        {
            g.setColour (c.withAlpha (0.10f * glow));
            g.strokePath (p, juce::PathStrokeType (width + 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (c.withAlpha (0.22f * glow));
            g.strokePath (p, juce::PathStrokeType (width + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

} // namespace samsi::ui
