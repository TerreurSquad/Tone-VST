#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class XtoneProcessor;

namespace hammer
{
std::unique_ptr<juce::Component> makeAmpPanel (XtoneProcessor&);
std::unique_ptr<juce::Component> makeLfoPanel (juce::AudioProcessorValueTreeState&, const juce::String& parameterGroupId);
}
