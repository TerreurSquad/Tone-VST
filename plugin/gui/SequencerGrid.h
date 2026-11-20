#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "StepSequencer.h"

class XtoneProcessor;

class SequencerGrid final : public juce::Component, public juce::TooltipClient, private juce::Timer
{
public:
    explicit SequencerGrid (XtoneProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void visibilityChanged() override;
    juce::String getTooltip() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    enum Row { On, Tie, Slice, Pitch, Velocity, rowCount };
    struct Cell { int step = -1; int row = -1; };

    void timerCallback() override;
    void rotate (int direction);
    void randomize();
    Cell cellAt (juce::Point<int>) const;
    juce::Rectangle<int> cellBounds (Cell) const;
    juce::String cellText (Cell) const;
    int value (Cell) const;
    void change (Cell, int);
    void startEdit (Cell);
    void endEdit (bool commit);
    int maxValue (int row) const;
    juce::String cellHelp (Cell) const;

    XtoneProcessor& proc;
    juce::ToggleButton enabled { "Sequencer On" }, reset { "Reset on Note" };
    juce::ComboBox length;
    juce::TextButton clear { "Clear" }, random { "Randomize" }, left, right;
    juce::TextEditor entry;
    juce::AudioProcessorValueTreeState::ButtonAttachment enabledAttachment, resetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> lengthAttachment;
    xt::Sequence steps;
    juce::Rectangle<int> numbers, rows;
    Cell focused, dragging, editing;
    int dragValue = 0, previousDragStep = -1;
    int count = 16, slices = 1, playhead = -1, assetVersion = -1;
};
