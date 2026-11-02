#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace samsi::ui
{

//==============================================================================
/** Farbpalette nach dem SAMSARA-Logo: tiefes Schwarz-Petrol, eisig leuchtendes Türkis,
    Gunmetal/Chrom; dazu Blau und Violett als Signalfarben (Bass) und psychedelische Akzente. */
namespace col
{
    const juce::Colour bgDeep   { 0xff020708 };
    const juce::Colour bgMid    { 0xff0a1b1f };
    const juce::Colour gunmetal { 0xff3e5557 };
    const juce::Colour panel    { 0xd0061114 };
    const juce::Colour blue     { 0xff2f7bff };
    const juce::Colour turq     { 0xff2fe3d6 };
    const juce::Colour cyan     { 0xffa8fff4 };   // Logo-Glühen
    const juce::Colour ice      { 0xff6fe8e0 };
    const juce::Colour violet   { 0xff8a5cff };
    const juce::Colour magenta  { 0xffe24bff };
    const juce::Colour amber    { 0xffffc24a };
    const juce::Colour red      { 0xffff4f6d };
    const juce::Colour green    { 0xff4dffa6 };
    const juce::Colour text     { 0xffe8f4ff };
    const juce::Colour textDim  { 0xff9fb6d6 };
    const juce::Colour chromeHi { 0xfff4f8ff };
    const juce::Colour chromeLo { 0xff56647e };

    // Signalfarben (in allen Ansichten gleich)
    const juce::Colour kick = turq;
    const juce::Colour bass = violet;
    const juce::Colour sum  = chromeHi;
}

juce::Font uiFont (float size, bool bold = false);

/** UTF-8-Text aus Quelltext-Literalen (Umlaute) sicher in juce::String umwandeln. */
inline juce::String T (const char* utf8) { return juce::String::fromUTF8 (utf8); }

/** Parametertyp, der Literale (UTF-8) und juce::String gleichermaßen annimmt. */
struct Text
{
    Text (const char* utf8) : s (juce::String::fromUTF8 (utf8)) {}
    Text (const juce::String& str) : s (str) {}
    operator const juce::String&() const noexcept { return s; }
    juce::String s;
};

/** Chrom-Verlauf für Rahmen, Knöpfe, Schrift. */
juce::ColourGradient chromeGradient (juce::Rectangle<float> r, bool vertical = true);

class SamsiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SamsiLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (13.0f); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }
    juce::Font getLabelFont (juce::Label&) override { return uiFont (13.0f); }
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
};

//==============================================================================
/** Drehregler mit Namen (oben) und Wert in Einheiten (unten), an einen Parameter gebunden. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const Text& label,
          juce::Colour accent = col::turq);

    void resized() override;
    juce::Slider& slider() noexcept { return knob; }

private:
    void updateValueText();

    juce::RangedAudioParameter* param = nullptr;
    juce::Slider knob;
    juce::Label name, value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** Ein/Aus-Schalter an einem Bool-Parameter. */
class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const Text& label);
    void resized() override { button.setBounds (getLocalBounds()); }
    juce::ToggleButton& get() noexcept { return button; }

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

/** Auswahlbox an einem Choice-Parameter, mit Beschriftung. */
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& apvts, const char* paramId, const Text& label);
    void resized() override;
    void setTooltip (const juce::String& t) { box.setTooltip (t); }
    /** Beschriftung links neben der Box statt darüber */
    void setLabelLeft (int width) { labelLeft = width; resized(); }

private:
    juce::Label name;
    juce::ComboBox box;
    int labelLeft = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** Glas-Panel mit Chrom-Rahmen und Titel. */
class PanelFrame : public juce::Component
{
public:
    PanelFrame (const Text& title, juce::Colour accent);
    void paint (juce::Graphics&) override;
    juce::Rectangle<int> content() const { return getLocalBounds().reduced (12).withTrimmedTop (26); }

    static void drawSectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const Text& text, juce::Colour c);

private:
    juce::String title;
    juce::Colour accent;
};

/** Registerkarten-Leiste im Logo-Stil (leuchtender Text, Tropfen-Markierung). */
class TabStrip : public juce::Component
{
public:
    TabStrip (const juce::StringArray& names, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void setSelected (int index, bool notify = true);
    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;

private:
    int indexAt (juce::Point<float> p) const;
    juce::StringArray names;
    juce::Colour accent;
    int selected = 0, hover = -1;
};

//==============================================================================
/** Symbole aus dem SAMSARA-Logo: Tropfen, Tropfen mit Mondsichel, Blütenkranz, Punktring. */
namespace sym
{
    /** Tropfen mit Spitze in Richtung angle (Bogenmaß, 0 = nach oben). */
    juce::Path drop (juce::Point<float> centre, float size, float angle = 0.0f);
    /** Logo-Mittelzeichen: Tropfen mit Auge über einer Mondsichel. */
    juce::Path lotusDrop (juce::Rectangle<float> area);
    void petalRing (juce::Graphics&, juce::Point<float> centre, float radius, int count, float petalSize,
                    float rotation, juce::Colour colour, float glow = 0.4f);
    void dottedRing (juce::Graphics&, juce::Point<float> centre, float radius, int count, float dotSize, juce::Colour colour);
    /** Linie mit weichem Leuchten (mehrere Durchgänge). */
    void glowStroke (juce::Graphics&, const juce::Path&, juce::Colour, float width, float glow = 1.0f);
}

/** Ordnet Komponenten in einem Raster mit festen Zellgrößen an. */
void layoutGrid (juce::Rectangle<int> area, const std::vector<juce::Component*>& items, int columns, int rowHeight);

} // namespace samsi::ui
