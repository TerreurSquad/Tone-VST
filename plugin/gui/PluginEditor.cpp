#include "PluginEditor.h"

#include "CottonCandyArtwork.h"
#include "FaceplateArtwork.h"

#include "../PluginProcessor.h"

using namespace juce;

class XtoneEditor::AboutOverlay : public Component
{
public:
    explicit AboutOverlay (XtoneEditor& owner) : editor (owner)
    {
        setTitle ("About XTONE");
        setDescription ("Extratone Retrigger Sampler. Version " JucePlugin_VersionString ". " JucePlugin_Manufacturer);
        setWantsKeyboardFocus (true);
        setFocusContainerType (FocusContainerType::keyboardFocusContainer);
        close.onClick = [this] { dismiss(); };
        addAndMakeVisible (close);

        for (size_t i = 0; i < xtp::editorSkins.size(); ++i)
            skins.addItem (xtp::editorSkins[i].name, (int) i + 1);
        skins.setTitle ("Skin");
        skins.setTooltip ("Choose a faceplate for this plugin instance.");
        skins.onChange = [this]
        {
            const int index = skins.getSelectedItemIndex();
            if (! isPositiveAndBelow (index, (int) xtp::editorSkins.size())) return;
            const auto skin = xtp::editorSkins[(size_t) index].skin;
            if (skin == editor.proc.getEditorSkin()) return;

            editor.proc.setEditorSkin (skin);
            editor.updateSkin();
            editor.proc.updateHostDisplay (AudioProcessorListener::ChangeDetails().withNonParameterStateChanged (true));
        };
        addAndMakeVisible (skins);
    }

    void show()
    {
        syncSelection();
        setVisible (true);
        toFront (true);
        skins.grabKeyboardFocus();
    }

    void syncSelection()
    {
        for (size_t i = 0; i < xtp::editorSkins.size(); ++i)
            if (xtp::editorSkins[i].skin == editor.theme.getSkin())
                skins.setSelectedItemIndex ((int) i, dontSendNotification);
    }

    void resized() override
    {
        card = getLocalBounds().withSizeKeepingCentre (352, 168);
        close.setBounds (card.getRight() - 77, card.getY() + 15, 61, 26);
        skins.setBounds (card.getX() + 81, card.getBottom() - 47, card.getWidth() - 99, 29);
    }

    void paint (Graphics& g) override
    {
        const auto& colours = hammer::panelColours (*this);
        g.fillAll (Colours::black.withAlpha (0.60f));
        g.setColour (colours.rubber);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (colours.edge);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.0f);

        const int x = card.getX() + 18;
        const int y = card.getY() + 13;
        g.setColour (hammer::accentFor (*this));
        g.setFont (hammer::font (26.0f, Font::bold));
        g.drawText ("XTONE", x, y, 220, 30, Justification::centredLeft);
        g.setColour (colours.text);
        g.setFont (hammer::font (15.0f));
        g.drawText ("Extratone Retrigger Sampler", x, y + 32, 250, 22, Justification::centredLeft);
        g.setColour (colours.muted);
        g.setFont (hammer::font (13.5f));
        g.drawText ("Version " JucePlugin_VersionString "  " JucePlugin_Manufacturer,
                    x, y + 55, 250, 19, Justification::centredLeft);
        g.setColour (colours.text);
        g.setFont (hammer::font (14.0f, Font::bold));
        g.drawText ("Skin", x, skins.getY(), 57, skins.getHeight(), Justification::centredLeft);
    }

    void mouseDown (const MouseEvent& event) override
    {
        if (! card.contains (event.getPosition())) dismiss();
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key != KeyPress::escapeKey) return false;
        dismiss();
        return true;
    }

private:
    void dismiss()
    {
        setVisible (false);
        editor.wordmark.grabKeyboardFocus();
    }

    XtoneEditor& editor;
    TextButton close { "Close" };
    ComboBox skins;
    Rectangle<int> card;
};

XtoneEditor::XtoneEditor (XtoneProcessor& p)
    : AudioProcessorEditor (p), proc (p), header (p), waveform (p), rate (p), params (p), sequence (p)
{
    addAndMakeVisible (canvas);
    canvas.addAndMakeVisible (header);
    canvas.addAndMakeVisible (waveform);
    canvas.addAndMakeVisible (rate);
    canvas.addChildComponent (params);
    canvas.addChildComponent (sequence);
    wordmark.getProperties().set ("wordmark", true);
    wordmark.setTitle ("About XTONE and skin settings");
    wordmark.setTooltip ("About XTONE and skin settings");
    wordmark.setMouseCursor (MouseCursor::PointingHandCursor);
    wordmark.onClick = [this] { about->show(); };
    canvas.addAndMakeVisible (wordmark);

    auto names = params.getPageNames();
    sequenceTab = names.size();
    names.add ("Sequencer");
    constexpr const char* tabHelp[] {
        "Choose the clock source and set the retrigger rate.",
        "Set MIDI note behavior, tuning, glide, and scale snapping.",
        "Set gate, overlap, jitter, unison voices, and the default hit ADSR.",
        "Set the main slice, playback window, and sample pitch.",
        "Alternate between the main slice and independent B playback.",
        "Sweep the retrigger rate after a MIDI note-on.",
        "Modulate rate, scan, gate, and sample pitch with LFO 1.",
        "Modulate rate, scan, gate, and sample pitch with LFO 2.",
        "Shape note amplitude and set output drive, clipping, and level.",
        "Edit the pattern that advances on every retrigger tick."
    };
    jassert (names.size() == (int) std::size (tabHelp));
    for (int i = 0; i < names.size(); ++i)
    {
        auto* key = keys.add (new TextButton (names[i]));
        key->getProperties().set ("key", true);
        key->getProperties().set ("teddyPrinted", i == 0 ? 2 : 1);
        key->setTooltip (tabHelp[i]);
        key->setRadioGroupId (2, dontSendNotification);
        key->setClickingTogglesState (true);
        key->onClick = [this, key, i]
        {
            if (key->getToggleState()) selectGroup (i);
        };
        canvas.addAndMakeVisible (key);
    }

    theme.setSkin (p.getEditorSkin());
    setLookAndFeel (&theme);
    selectGroup (0);
    setResizable (true, true);
    setResizeLimits (760, 560, 2400, 1800);
    about = std::make_unique<AboutOverlay> (*this);
    addChildComponent (*about);
    setSize (1000, 720);
    startTimerHz (12);
}

XtoneEditor::~XtoneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void XtoneEditor::timerCallback() { updateSkin(); }

void XtoneEditor::updateSkin()
{
    const auto skin = proc.getEditorSkin();
    if (theme.getSkin() == skin) return;
    theme.setSkin (skin);
    sendLookAndFeelChange();
    about->syncSelection();
    resized();
}

void XtoneEditor::selectGroup (int index)
{
    keys[index]->setToggleState (true, dontSendNotification);
    rate.setVisible (index == 0);
    params.setVisible (index != 0 && index != sequenceTab);
    sequence.setVisible (index == sequenceTab);
    if (index != sequenceTab) params.showGroup (index);
    resized();
    repaint();
}

void XtoneEditor::paint (Graphics& g)
{
    const auto skin = theme.getSkin();
    if (skin == xtp::EditorSkin::cottonCandy)
    {
        if (getWidth() * canvas.getHeight() != getHeight() * canvas.getWidth())
            hammer::cottonCandy::drawSurround (g, getLocalBounds());
        g.addTransform (canvas.getTransform());
        hammer::cottonCandy::drawFaceplate (g, rate.isVisible(), sequence.isVisible(), rate.showsDivisions());
        if (params.isVisible()) hammer::artwork::drawRatePanel (g, params.getBounds(), skin);
        return;
    }
    const auto dock = header.presetDockBounds().translated (header.getX(), header.getY());
    hammer::artwork::drawFrame (g, getLocalBounds(), waveFrame, rate.getBounds(), dock, skin);
    if (params.isVisible()) hammer::artwork::drawRatePanel (g, params.getBounds(), skin);
    hammer::artwork::drawKeyWell (g, keyWell, skin);
}

void XtoneEditor::resized()
{
    if (getLocalBounds().isEmpty()) return;
    if (about != nullptr) about->setBounds (getLocalBounds());
    if (theme.getSkin() == xtp::EditorSkin::cottonCandy)
    {
        layoutTeddyBelly();
        return;
    }
    canvas.setTransform (AffineTransform());
    canvas.setBounds (getLocalBounds());
    waveform.setTeddyLayout (false);
    auto content = canvas.getLocalBounds().reduced (28, 20);
    if (content.isEmpty()) return;
    header.setBounds (content.removeFromTop (57));
    content.removeFromTop (10);
    content.removeFromBottom (74);
    keyWell = hammer::artwork::keyArea (getLocalBounds(), content.getBottom());
    auto keyRow = keyWell.reduced (2, 1);
    constexpr auto gap = 3;
    int totalWeight = 0;
    for (auto* key : keys) totalWeight += key->getButtonText().length() + 5;
    int space = keyRow.getWidth() - (keys.size() - 1) * gap;
    for (auto* key : keys)
    {
        const int weight = key->getButtonText().length() + 5;
        const int width = space * weight / totalWeight;
        key->setBounds (keyRow.removeFromLeft (width));
        keyRow.removeFromLeft (gap);
        space -= width;
        totalWeight -= weight;
    }

    auto body = content;
    waveFrame = content.removeFromLeft (content.getWidth() * 61 / 100);
    content.removeFromLeft (10);
    rate.setBounds (content);
    params.setBounds (content);
    if (sequence.isVisible())
    {
        sequence.setBounds (body.removeFromBottom (jlimit (185, 225, body.getHeight() / 2)));
        body.removeFromBottom (8);
        waveform.setBounds (body);
    }
    else waveform.setBounds (waveFrame);
    const auto dockX = header.getX() + header.presetDockBounds().getX();
    wordmark.setBounds (hammer::artwork::wordmarkBounds (waveform.getY(), dockX, theme.getSkin()));
}

void XtoneEditor::layoutTeddyBelly()
{
    using namespace hammer::cottonCandy;
    canvas.setBounds (0, 0, canvasWidth, canvasHeight);
    canvas.setTransform (RectanglePlacement (RectanglePlacement::centred)
                             .getTransformToFit (canvas.getLocalBounds().toFloat(), getLocalBounds().toFloat()));

    header.setBounds (596, 36, 348, 65);
    wordmark.setBounds (65, 30, 313, 68);
    const Rectangle<int> slots[] {
        { 62, 628, 78, 34 }, { 142, 628, 67, 34 }, { 211, 628, 73, 34 },
        { 285, 628, 74, 34 }, { 361, 628, 75, 34 }, { 558, 628, 77, 34 },
        { 636, 628, 73, 34 }, { 712, 628, 70, 34 }, { 783, 628, 65, 34 }, { 849, 628, 93, 34 }
    };
    jassert (keys.size() == (int) std::size (slots));
    for (int i = 0; i < keys.size() && i < (int) std::size (slots); ++i)
        keys[i]->setBounds (slots[i]);

    waveFrame = { 37, 109, 575, 503 };
    rate.setBounds (607, 109, 358, 503);
    params.setBounds (rate.getBounds());
    waveform.setTeddyLayout (! sequence.isVisible());
    if (sequence.isVisible())
    {
        waveform.setBounds (37, 112, 928, 271);
        sequence.setBounds (37, 391, 928, 217);
    }
    else waveform.setBounds (waveFrame);
}

bool XtoneEditor::isInterestedInFileDrag (const StringArray& files)
{
    if (about->isVisible()) return false;
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
    return false;
}

void XtoneEditor::filesDropped (const StringArray& files, int, int)
{
    if (about->isVisible()) return;
    for (auto& f : files)
        if (File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3"))
        {
            proc.loadSampleFile (File (f));
            return;
        }
}
