#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "HammerLookAndFeel.h"
#include "HeaderBar.h"
#include "ParamPanel.h"
#include "RatePanel.h"
#include "SequencerGrid.h"
#include "WaveformView.h"

class XtoneProcessor;

class XtoneEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit XtoneEditor (XtoneProcessor&);
    ~XtoneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    class AboutOverlay;
    void timerCallback() override;
    void updateSkin();
    void selectGroup (int);
    void layoutTeddyBelly();

    XtoneProcessor& proc;
    hammer::LookAndFeel theme;
    juce::Component canvas;
    HeaderBar header;
    WaveformView waveform;
    RatePanel rate;
    ParamPanel params;
    SequencerGrid sequence;
    juce::Rectangle<int> waveFrame;
    int sequenceTab = -1;
    juce::TextButton wordmark { "XTONE" };
    juce::OwnedArray<juce::TextButton> keys;
    juce::Rectangle<int> keyWell;
    std::unique_ptr<AboutOverlay> about;
    juce::TooltipWindow tooltips { this };
};
