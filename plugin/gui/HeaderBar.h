#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class XtoneProcessor;

class HeaderBar : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar (XtoneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Rectangle<int> presetDockBounds() const;

private:
    void timerCallback() override;
    void choosePresetFile (bool saving);

    XtoneProcessor& proc;
    juce::ComboBox presets;
    juce::TextButton load { juce::String::fromUTF8 ("Load…") }, save { juce::String::fromUTF8 ("Save…") };
    std::unique_ptr<juce::FileChooser> chooser;
};
