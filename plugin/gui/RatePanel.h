#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

class XtoneProcessor;

class RatePanel : public juce::Component, private juce::Timer
{
public:
    bool showsDivisions() const noexcept { return source != 1; }
    explicit RatePanel (XtoneProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    void layoutTeddyBelly();
    void timerCallback() override;
    void setSource (int);

    using State = juce::AudioProcessorValueTreeState;
    XtoneProcessor& proc;
    std::array<juce::TextButton, 3> sources;
    std::array<juce::Slider, 3> dials;
    juce::Slider smoothing { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox division, modifier;
    juce::ToggleButton phaseLock { "Host Phase Lock" };
    juce::Label rateTitle, frequency, detail, pitch, smoothingLabel, divisionLabel, modifierLabel;
    juce::Rectangle<int> display;
    std::array<std::unique_ptr<State::SliderAttachment>, 3> dialAttachments;
    std::unique_ptr<State::SliderAttachment> smoothingAttachment;
    std::unique_ptr<State::ComboBoxAttachment> divisionAttachment, modifierAttachment;
    std::unique_ptr<State::ButtonAttachment> phaseAttachment;
    std::unique_ptr<juce::ParameterAttachment> sourceAttachment;
    int source = -1;
};
