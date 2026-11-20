#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class XtoneProcessor;

/// Parameter pages for the parameter groups without a dedicated editor.
class ParamPanel : public juce::Component
{
public:
    explicit ParamPanel (XtoneProcessor&);
    const juce::StringArray& getPageNames() const { return pageNames; }
    void showGroup (int index);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::OwnedArray<juce::Component> pages;
    juce::StringArray pageNames { "Rate" };
    int selected = -1;
};
