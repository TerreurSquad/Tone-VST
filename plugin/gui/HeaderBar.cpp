#include "HeaderBar.h"

#include "FaceplateArtwork.h"

#include "HammerLookAndFeel.h"
#include "../PluginProcessor.h"

using namespace juce;

HeaderBar::HeaderBar (XtoneProcessor& p) : proc (p)
{
    addAndMakeVisible (presets);
    addAndMakeVisible (load);
    addAndMakeVisible (save);
    presets.getProperties().set ("teddyField", true);
    load.getProperties().set ("teddyPrinted", 1);
    save.getProperties().set ("teddyPrinted", 1);
    presets.setTitle ("Factory Preset");
    load.setTitle ("Load preset from file");
    save.setTitle ("Save preset to file");
    presets.setTooltip ("Apply a factory sound to the current sample.");
    load.setTooltip ("Restore the sound, sample, slices, and sequence from an .xtone file.");
    save.setTooltip ("Save the sound, sample, slices, and sequence as an .xtone file.");
    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presets.addItem (proc.getProgramName (i), i + 1);
    timerCallback();
    presets.onChange = [this]
    {
        const int index = presets.getSelectedItemIndex();
        if (index >= 0 && index != proc.getCurrentProgram())
            proc.setCurrentProgram (index);
    };
    load.onClick = [this] { choosePresetFile (false); };
    save.onClick = [this] { choosePresetFile (true); };
    startTimerHz (12);
}

void HeaderBar::choosePresetFile (bool saving)
{
    if (chooser != nullptr) return;
    chooser = std::make_unique<FileChooser> (saving ? "Save Preset" : "Load Preset", File(), "*.xtone");
    const auto flags = saving ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting
                              : FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (flags, [self = SafePointer<HeaderBar> (this), saving] (const FileChooser& dialog)
    {
        if (self == nullptr) return;
        const auto file = dialog.getResult();
        self->chooser.reset();
        if (saving && file != File())
            self->proc.saveStateToFile (file.withFileExtension ("xtone"));
        else if (! saving && file.existsAsFile())
            self->proc.loadStateFromFile (file);
    });
}

void HeaderBar::timerCallback()
{
    const int current = proc.getCurrentProgram();
    if (presets.getSelectedItemIndex() != current)
        presets.setSelectedItemIndex (current, dontSendNotification);
}

Rectangle<int> HeaderBar::presetDockBounds() const
{
    auto area = getLocalBounds().withTrimmedRight (35);
    return area.removeFromRight (jlimit (310, 390, area.getWidth() / 2)).reduced (0, 5);
}

void HeaderBar::resized()
{
    if (hammer::skinFor (*this) == xtp::EditorSkin::cottonCandy)
    {
        presets.setBounds (75, 19, 128, 28);
        load.setBounds (208, 19, 61, 28);
        save.setBounds (272, 19, 61, 28);
        return;
    }
    auto dock = presetDockBounds().reduced (8, 6);
    dock.removeFromLeft (56);
    save.setBounds (dock.removeFromRight (73));
    dock.removeFromRight (4);
    load.setBounds (dock.removeFromRight (67));
    dock.removeFromRight (4);
    presets.setBounds (dock);
}

void HeaderBar::paint (Graphics& g)
{
    const auto dock = presetDockBounds();
    const auto skin = hammer::skinFor (*this);
    if (skin == xtp::EditorSkin::cottonCandy) return;
    hammer::artwork::drawPresetDock (g, dock, skin);
    g.setColour (hammer::ink);
    g.setFont (hammer::font (11.0f, Font::bold));
    g.drawText ("PRESET", dock.withTrimmedLeft (14), Justification::centredLeft);
}
