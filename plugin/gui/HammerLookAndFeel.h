#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../EditorSkin.h"

namespace hammer
{
inline const juce::Colour yellow { 0xffffca18 };
inline const juce::Colour ink { 0xff171813 };
inline const juce::Colour rubber { 0xff242624 };
inline const juce::Colour recess { 0xff121413 };
inline const juce::Colour edge { 0xff464941 };
inline const juce::Colour text { 0xfff4f2e8 };
inline const juce::Colour muted { 0xffb3b6aa };

struct PanelColours
{
    juce::Colour text, muted, recess, edge, rubber;
};

const PanelColours& panelColours (xtp::EditorSkin);
const PanelColours& panelColours (const juce::Component&);
juce::Colour accentColour (xtp::EditorSkin);
juce::Colour ratePanelText (xtp::EditorSkin);
xtp::EditorSkin skinFor (const juce::Component&);
juce::Colour accentFor (const juce::Component&);
juce::Font font (float height, int style = juce::Font::plain);
juce::Font mono (float height, int style = juce::Font::plain);
juce::Path cutCorners (juce::Rectangle<float>, float cut);
void drawRecess (juce::Graphics&, juce::Rectangle<float>, xtp::EditorSkin);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void setSkin (xtp::EditorSkin);
    xtp::EditorSkin getSkin() const noexcept { return skin; }
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool, int, int, int, int, juce::ComboBox&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                           juce::Slider&) override;

private:
    xtp::EditorSkin skin = xtp::EditorSkin::hammerDrill;
    juce::Colour accent = yellow;
};
} // namespace hammer
