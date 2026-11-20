#include "RatePanel.h"

#include "FaceplateArtwork.h"

#include "HammerLookAndFeel.h"
#include "../PluginProcessor.h"
#include "RateMath.h"

using namespace juce;

namespace
{
String formatRateNumber (double value, int decimals)
{
    const String formatted (value, decimals);
    const auto integer = formatted.upToFirstOccurrenceOf (".", false, false);
    String result;
    for (int i = 0; i < integer.length(); ++i)
    {
        if (i > 0 && (integer.length() - i) % 3 == 0) result << ",";
        result << integer[i];
    }
    if (decimals > 0) result << "." << formatted.fromFirstOccurrenceOf (".", false, false);
    return result;
}

void setupLabel (Label& label, const String& labelText, float height, Colour colour)
{
    label.setFont (hammer::font (height, Font::bold));
    label.setColour (Label::textColourId, colour);
    label.setText (labelText, dontSendNotification);
    label.setMinimumHorizontalScale (0.8f);
    label.setBorderSize (BorderSize<int> (0));
}
} // namespace

RatePanel::RatePanel (XtoneProcessor& p) : proc (p)
{
    auto& state = proc.apvts;
    auto* sourceParam = state.getParameter ("rate_src");
    const auto choices = sourceParam->getAllValueStrings();
    const std::array<const char*, 3> ids { "bpm", "hz", "host_mult" };

    constexpr const char* sourceHelp[] {
        "Use an independent plugin tempo and a note division.",
        "Set the retrigger frequency directly in Hertz.",
        "Multiply the host tempo and apply a note division."
    };
    constexpr const char* dialHelp[] {
        "Set the plugin tempo. Division sets how many hits occur for each beat.",
        "Set hits per second. In Absolute key tracking, MIDI notes supply the rate.",
        "Multiply the project tempo before applying Division."
    };
    for (size_t i = 0; i < sources.size(); ++i)
    {
        auto& button = sources[i];
        button.setButtonText (i == 2 ? String::fromUTF8 ("Host ×") : choices[(int) i]);
        button.setTooltip (sourceHelp[i]);
        button.setRadioGroupId (1, dontSendNotification);
        button.setClickingTogglesState (true);
        button.getProperties().set ("teddyPrinted", i == 0 ? 2 : 1);
        button.onClick = [this, i]
        {
            if (sources[i].getToggleState())
                sourceAttachment->setValueAsCompleteGesture ((float) i);
        };
        addAndMakeVisible (button);

        auto& dial = dials[i];
        auto* param = state.getParameter (ids[i]);
        dial.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        dial.setTextBoxStyle (Slider::TextBoxBelow, i == 2, 136, 25);
        dial.setName (param->getName (64));
        dial.setTooltip (dialHelp[i]);
        dial.getProperties().set ("chuck", true);
        addChildComponent (dial);
        dialAttachments[i] = std::make_unique<State::SliderAttachment> (state, ids[i], dial);
        if (i == 1)
        {
            dial.textFromValueFunction = [] (double value) { return String (value, 2) + " Hz"; };
            dial.updateText();
        }
    }

    for (auto* label : { &rateTitle, &frequency, &detail, &pitch, &smoothingLabel, &divisionLabel, &modifierLabel })
        addAndMakeVisible (*label);
    setupLabel (rateTitle, {}, 13.0f, hammer::ink);
    setupLabel (detail, {}, 13.0f, hammer::muted);
    setupLabel (pitch, {}, 13.0f, hammer::text);
    setupLabel (smoothingLabel, "Rate Smoothing", 13.0f, hammer::ink);
    setupLabel (divisionLabel, "Division", 12.5f, hammer::ink);
    setupLabel (modifierLabel, "Division Type", 12.5f, hammer::ink);
    frequency.setColour (Label::textColourId, hammer::text);
    frequency.setMinimumHorizontalScale (0.7f);
    frequency.setBorderSize (BorderSize<int> (0));
    detail.setJustificationType (Justification::centredLeft);
    pitch.setJustificationType (Justification::centredRight);

    division.setTooltip ("Set the note length between retrigger ticks.");
    divisionLabel.setTooltip (division.getTooltip());
    modifier.setTooltip ("Choose a straight, triplet, or dotted division.");
    modifierLabel.setTooltip (modifier.getTooltip());
    phaseLock.setTooltip ("Align the clock when host playback starts or jumps.");
    smoothing.setTooltip ("Smooth changes to the base rate. Set to zero for immediate jumps.");
    smoothingLabel.setTooltip (smoothing.getTooltip());
    frequency.setTooltip ("Retrigger ticks per second. At high rates this determines the audible pitch.");
    detail.setTooltip ("Tempo in BPM if each retrigger were a quarter note.");
    pitch.setTooltip ("Nearest note in scientific and FL Studio notation, with tuning offset in cents.");

    division.setTitle (divisionLabel.getText());
    modifier.setTitle (modifierLabel.getText());
    division.getProperties().set ("teddyField", true);
    modifier.getProperties().set ("teddyField", true);
    division.addItemList (state.getParameter ("division")->getAllValueStrings(), 1);
    modifier.addItemList (state.getParameter ("div_mod")->getAllValueStrings(), 1);
    addAndMakeVisible (division);
    addAndMakeVisible (modifier);
    addAndMakeVisible (phaseLock);
    phaseLock.setColour (ToggleButton::textColourId, hammer::ink);
    phaseLock.setColour (ToggleButton::tickColourId, hammer::ink);

    smoothing.setName (smoothingLabel.getText());
    smoothing.setTextBoxStyle (Slider::TextBoxRight, false, 68, 24);
    addAndMakeVisible (smoothing);

    divisionAttachment = std::make_unique<State::ComboBoxAttachment> (state, "division", division);
    modifierAttachment = std::make_unique<State::ComboBoxAttachment> (state, "div_mod", modifier);
    phaseAttachment = std::make_unique<State::ButtonAttachment> (state, "host_lock", phaseLock);
    smoothingAttachment = std::make_unique<State::SliderAttachment> (state, "rate_smooth", smoothing);
    sourceAttachment = std::make_unique<ParameterAttachment> (*sourceParam, [this] (float value) { setSource ((int) value); });
    sourceAttachment->sendInitialUpdate();
    startTimerHz (30);
}

void RatePanel::setSource (int next)
{
    source = jlimit (0, 2, next);
    for (size_t i = 0; i < dials.size(); ++i)
    {
        const bool selected = (int) i == source;
        sources[i].setToggleState (selected, dontSendNotification);
        dials[i].setVisible (selected);
    }

    const bool hasDivision = showsDivisions();
    for (Component* component : std::array<Component*, 4> { &division, &modifier, &divisionLabel, &modifierLabel })
        component->setVisible (hasDivision);
    phaseLock.setVisible (source == 2);
    resized();
    timerCallback();
    repaint();
}

void RatePanel::resized()
{
    if (hammer::skinFor (*this) == xtp::EditorSkin::cottonCandy)
    {
        layoutTeddyBelly();
        return;
    }
    auto r = getLocalBounds().reduced (16, 12);
    if (r.isEmpty()) return;

    smoothingLabel.setVisible (true);
    divisionLabel.setVisible (showsDivisions());
    modifierLabel.setVisible (showsDivisions());

    rateTitle.setBounds (r.removeFromTop (19));
    display = r.removeFromTop (76);
    auto digits = display.reduced (10, 6);
    auto secondary = digits.removeFromBottom (19);
    detail.setBounds (secondary.removeFromLeft (secondary.getWidth() * 45 / 100));
    pitch.setBounds (secondary);
    frequency.setFont (hammer::font (jlimit (31.0f, 49.0f, (float) digits.getWidth() * 0.15f), Font::bold));
    frequency.setBounds (digits);

    r.removeFromTop (8);
    auto selectors = r.removeFromTop (30);
    for (size_t i = 0; i < sources.size(); ++i)
    {
        sources[i].setBounds (selectors.removeFromLeft (selectors.getWidth() / (int) (sources.size() - i)).withTrimmedRight (i == 2 ? 0 : 3));
    }
    r.removeFromTop (5);

    auto smoothingRow = r.removeFromBottom (27);
    smoothingLabel.setBounds (smoothingRow.removeFromLeft (105));
    smoothing.setBounds (smoothingRow);
    if (source != 1)
    {
        if (source == 2)
            phaseLock.setBounds (r.removeFromBottom (24));
        auto options = r.removeFromBottom (47);
        auto div = options.removeFromLeft (options.getWidth() * 2 / 5).withTrimmedRight (7);
        divisionLabel.setBounds (div.removeFromTop (18));
        modifierLabel.setBounds (options.removeFromTop (18));
        division.setBounds (div.withTrimmedBottom (4));
        modifier.setBounds (options.withTrimmedBottom (4));
    }

    r.removeFromBottom (5);
    for (auto& dial : dials)
        dial.setBounds (r.withSizeKeepingCentre (jmin (r.getWidth(), r.getHeight()), r.getHeight()));
}

void RatePanel::layoutTeddyBelly()
{
    divisionLabel.setVisible (false);
    modifierLabel.setVisible (false);
    smoothingLabel.setVisible (false);
    rateTitle.setBounds (29, 19, 153, 17);
    frequency.setFont (hammer::font (36.0f, Font::bold));
    frequency.setBounds (41, 42, 273, 36);
    detail.setBounds (41, 78, 129, 17);
    pitch.setBounds (172, 78, 143, 17);
    sources[0].setBounds (27, 105, 98, 29);
    sources[1].setBounds (129, 105, 98, 29);
    sources[2].setBounds (230, 105, 97, 29);

    for (auto& dial : dials)
        dial.setBounds (32, 135, 278, source == 2 ? 249 : 273);
    phaseLock.setBounds (102, 386, 209, 22);
    division.setBounds (26, 424, 110, 23);
    modifier.setBounds (147, 424, 180, 23);
    smoothing.setBounds (119, 450, 208, 25);
}

void RatePanel::paint (Graphics& g)
{
    const auto skin = hammer::skinFor (*this);
    if (skin == xtp::EditorSkin::cottonCandy) return;
    hammer::artwork::drawRatePanel (g, getLocalBounds(), skin);
    hammer::artwork::drawRateDisplay (g, display, skin);
}

void RatePanel::lookAndFeelChanged()
{
    const auto skin = hammer::skinFor (*this);
    const auto colour = hammer::ratePanelText (skin);
    for (auto* label : { &rateTitle, &smoothingLabel, &divisionLabel, &modifierLabel })
        label->setColour (Label::textColourId, colour);
    phaseLock.setColour (ToggleButton::textColourId, colour);
    phaseLock.setColour (ToggleButton::tickColourId, colour);
    const auto& colours = hammer::panelColours (skin);
    frequency.setColour (Label::textColourId, colours.text);
    pitch.setColour (Label::textColourId, colours.text);
    detail.setColour (Label::textColourId, colours.muted);
    resized();
}

void RatePanel::timerCallback()
{
    const auto& telemetry = proc.telemetry();
    const auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const bool active = telemetry.active.load();
    double hz = active ? (double) telemetry.rateHz.load() : 0.0;
    if (! active && source == 0)
        hz = xt::bpmToHz (raw ("bpm"), (int) raw ("division"), (int) raw ("div_mod"));
    else if (! active && source == 1)
        hz = raw ("hz");

    rateTitle.setText (active ? "LIVE RATE" : "BASE RATE", dontSendNotification);
    frequency.setText (hz > 0.0 ? formatRateNumber (hz, 2) + " Hz" : String::fromUTF8 ("— Hz"), dontSendNotification);
    detail.setText (hz > 0.0 ? formatRateNumber (xt::hzToBpmEquivalent (hz), 0) + " BPM eq." : "Host Tempo", dontSendNotification);
    if (hz > 0.0)
    {
        const double note = xt::hzToNote (hz, raw ("a4_ref"));
        const auto scientific = String (xt::noteName (note, false)).upToFirstOccurrenceOf (" ", false, false);
        pitch.setText (scientific + " / FL " + String (xt::noteName (note, true)), dontSendNotification);
    }
    else
    {
        pitch.setText ({}, dontSendNotification);
    }
}
