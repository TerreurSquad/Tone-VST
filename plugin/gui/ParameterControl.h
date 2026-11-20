#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class ParameterControl final : public juce::Component
{
public:
    enum class Style { bar, knob, slice };
    ParameterControl (juce::RangedAudioParameter&, juce::AudioProcessorValueTreeState&,
                      juce::String caption = {}, bool stacked = false, Style style = Style::bar);
    ~ParameterControl() override;
    void setSliceCount (int count);
    int preferredHeight() const noexcept { return style == Style::knob ? 96 : 36; }
    void resized() override;
    void lookAndFeelChanged() override;

private:
    using State = juce::AudioProcessorValueTreeState;
    juce::Label label;
    Style style;
    bool stacked;
    std::unique_ptr<juce::Slider> value;
    std::unique_ptr<juce::ComboBox> choice;
    std::unique_ptr<juce::ToggleButton> toggle;
    std::unique_ptr<State::SliderAttachment> valueAttachment;
    std::unique_ptr<State::ComboBoxAttachment> choiceAttachment;
    std::unique_ptr<State::ButtonAttachment> toggleAttachment;
    std::unique_ptr<juce::ParameterAttachment> sliceAttachment;
    std::unique_ptr<juce::Label> onlySlice;
    int sliceCount = 0, storedSlice = 0;
    bool sliceGesture = false;
};
