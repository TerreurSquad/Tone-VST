#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../EditorSkin.h"

namespace hammer::artwork
{
void drawFrame (juce::Graphics&, juce::Rectangle<int> bounds, juce::Rectangle<int> waveform,
                juce::Rectangle<int> rate, juce::Rectangle<int> presetDock, xtp::EditorSkin);
juce::Rectangle<int> wordmarkBounds (int headerHeight, int dockX, xtp::EditorSkin);
void drawWaveformPanel (juce::Graphics&, juce::Rectangle<int>, xtp::EditorSkin);
void drawRatePanel (juce::Graphics&, juce::Rectangle<int>, xtp::EditorSkin);
void drawPresetDock (juce::Graphics&, juce::Rectangle<int>, xtp::EditorSkin);
void drawRateDisplay (juce::Graphics&, juce::Rectangle<int>, xtp::EditorSkin);
void drawButton (juce::Graphics&, juce::Rectangle<int>, bool selected, bool key, xtp::EditorSkin = xtp::EditorSkin::hammerDrill);
void drawSlider (juce::Graphics&, juce::Rectangle<int> track, juce::Rectangle<float> grip, bool enabled, bool focused,
                 xtp::EditorSkin);
juce::Rectangle<int> keyArea (juce::Rectangle<int> bounds, int bodyBottom);
void drawKeyWell (juce::Graphics&, juce::Rectangle<int>, xtp::EditorSkin);
void drawKnob (juce::Graphics&, juce::Rectangle<float>, float angle, xtp::EditorSkin);
} // namespace hammer::artwork
