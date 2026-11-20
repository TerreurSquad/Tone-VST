#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hammer::cottonCandy
{
inline const juce::Colour berry { 0xff54243e };
inline const juce::Colour rose { 0xffbc286b };
inline const juce::Colour blue { 0xffc6e7ff };

inline constexpr int canvasWidth = 1000;
inline constexpr int canvasHeight = 720;

void drawSurround (juce::Graphics&, juce::Rectangle<int>);
void drawFaceplate (juce::Graphics&, bool ratePage, bool sequencerPage, bool showDivisions);
bool usesPrintedCap (const juce::Button&);
void drawPearl (juce::Graphics&, juce::Rectangle<float>, juce::Colour top, juce::Colour bottom,
                float radius);
void drawQuiltedPanel (juce::Graphics&, juce::Rectangle<float>);
void drawButton (juce::Graphics&, juce::Rectangle<int>, bool selected);
void drawSlider (juce::Graphics&, juce::Rectangle<int> track, juce::Rectangle<float> grip,
                 bool enabled, bool focused);
void drawKnob (juce::Graphics&, juce::Rectangle<float>, float angle, float start, float end,
               bool focused, bool mainDial);
} // namespace hammer::cottonCandy
