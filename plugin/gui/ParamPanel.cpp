#include "ParamPanel.h"

#include "HammerLookAndFeel.h"
#include "ModulationPanels.h"
#include "ParameterControl.h"
#include "../PluginProcessor.h"

#include <cmath>
#include <initializer_list>
#include <utility>

using namespace juce;

namespace
{
class GroupPage : public Component, private Timer
{
public:
    explicit GroupPage (XtoneProcessor& p) : processor (p) {}

    void addSection (String title, std::initializer_list<const char*> ids)
    {
        auto& section = addSection (std::move (title));
        for (const auto* id : ids) addControl (section, *processor.apvts.getParameter (id));
    }

    void addGroup (const char* id, String title = {})
    {
        auto& section = addSection (std::move (title));
        for (const auto* group : processor.getParameterTree().getSubgroups (false))
            if (group->getID() == id)
                for (auto* p : group->getParameters (true))
                    if (auto* parameter = dynamic_cast<RangedAudioParameter*> (p)) addControl (section, *parameter);
    }

    void addRampReadout()
    {
        rampReadout.setFont (hammer::mono (13.0f));
        rampReadout.setTooltip ("Ramp endpoints in BPM equivalent, before LFOs and final rate snapping.");
        addAndMakeVisible (rampReadout);
        startTimerHz (30);
    }

    void lookAndFeelChanged() override
    {
        const auto& colours = hammer::panelColours (*this);
        rampReadout.setColour (Label::textColourId, colours.text);
        for (auto* section : sections)
            section->heading.setColour (Label::textColourId, colours.muted);
    }

    void layoutFor (int width)
    {
        int y = 0;
        for (auto* section : sections)
        {
            if (section->heading.isVisible())
            {
                section->heading.setBounds (4, y, width - 8, 20);
                y += 21;
            }
            const auto& controls = section->controls;
            for (int i = 0; i < controls.size(); i += 2)
            {
                auto* first = controls[i];
                auto* second = controls[i + 1];
                const int firstWidth = second != nullptr ? width / 2 : width;
                const int height = jmax (first->preferredHeight(), second != nullptr ? second->preferredHeight() : 0);
                first->setBounds (0, y + (height - first->preferredHeight()) / 2, firstWidth, first->preferredHeight());
                if (second != nullptr)
                    second->setBounds (firstWidth, y + (height - second->preferredHeight()) / 2, width - firstWidth, second->preferredHeight());
                y += height + 3;
            }
            y += 7;
        }
        if (rampReadout.getParentComponent() != nullptr)
        {
            rampReadout.setBounds (3, y, width - 6, 26);
            y += 26;
        }
        setSize (width, y);
    }

private:
    struct Section
    {
        Label heading;
        OwnedArray<ParameterControl> controls;
    };

    Section& addSection (String title)
    {
        auto* section = sections.add (new Section);
        if (title.isNotEmpty())
        {
            section->heading.setText (title, dontSendNotification);
            section->heading.setFont (hammer::mono (11.0f, Font::bold));
            section->heading.setBorderSize (BorderSize<int> (0));
            addAndMakeVisible (section->heading);
        }
        return *section;
    }

    void addControl (Section& section, RangedAudioParameter& parameter)
    {
        const auto id = parameter.getParameterID();
        using Style = ParameterControl::Style;
        const bool slice = id == "slice" || id == "ab_slice";
        const bool knob = id == "transpose" || id == "fine" || id == "s_pitch" || id == "penv_amt"
                       || id == "ab_sample_pitch" || id == "ab_penv_amt";
        const auto style = slice ? Style::slice : knob ? Style::knob : Style::bar;
        auto* control = section.controls.add (new ParameterControl (parameter, processor.apvts, {}, true, style));
        addAndMakeVisible (*control);
        if (slice)
        {
            sliceControls.add (control);
            updateSliceCounts();
            if (! isTimerRunning()) startTimerHz (30);
        }
    }

    void timerCallback() override
    {
        if (! sliceControls.isEmpty() && assetVersion != processor.getAssetVersion()) updateSliceCounts();
        if (rampReadout.getParentComponent() != nullptr && isShowing())
        {
            const auto raw = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };
            const double base = processor.telemetry().rampBaseHz.load();
            const auto endpoint = [&] (const char* id)
            {
                return String (60.0 * base * std::exp2 ((double) raw (id)), 1);
            };
            rampReadout.setText (base > 0.0 ? endpoint ("ramp_start") + String::fromUTF8 (" → ") + endpoint ("ramp_end") + " BPM eq."
                                           : String::fromUTF8 ("Ramp endpoints · BPM eq."), dontSendNotification);
        }
    }

    void updateSliceCounts()
    {
        assetVersion = processor.getAssetVersion();
        const auto asset = processor.getCurrentAsset();
        for (auto* control : sliceControls) control->setSliceCount (asset != nullptr ? asset->slices.numSlices() : 1);
    }

    XtoneProcessor& processor;
    Label rampReadout;
    OwnedArray<Section> sections;
    Array<ParameterControl*> sliceControls;
    int assetVersion = -1;
};

class ScrollingPage : public Component
{
public:
    explicit ScrollingPage (XtoneProcessor& p) : page (p)
    {
        view.setViewedComponent (&page, false);
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);
    }
    void resized() override
    {
        view.setBounds (getLocalBounds());
        page.layoutFor (getWidth() - view.getScrollBarThickness());
    }

    GroupPage page;

private:
    Viewport view;
};
} // namespace

ParamPanel::ParamPanel (XtoneProcessor& p)
{
    const auto addPage = [this] (const char* name, std::unique_ptr<Component> page)
    {
        pageNames.add (name);
        addChildComponent (pages.add (page.release()));
    };

    auto pitch = std::make_unique<ScrollingPage> (p);
    pitch->page.addSection ("MIDI & TUNING", { "midi_mode", "key_mode", "key_root", "slice_base", "transpose", "fine",
                                               "a4_ref", "pb_range", "glide", "glide_legato", "phase_reset", "vel_sens" });
    pitch->page.addGroup ("snap", "TUNE SNAP");
    addPage ("Pitch", std::move (pitch));

    auto trigger = std::make_unique<ScrollingPage> (p);
    trigger->page.addGroup ("retrigger", "RETRIGGER");
    trigger->page.addGroup ("hit_envelope", "TRIGGER ADSR");
    trigger->page.addGroup ("unison", "UNISON");
    addPage ("Trigger", std::move (trigger));

    auto sample = std::make_unique<ScrollingPage> (p);
    sample->page.addSection ("PLAYBACK", { "slice", "interp", "scan", "length", "reverse", "mono" });
    sample->page.addSection ("SAMPLE PITCH", { "s_pitch", "penv_amt", "s_fine", "penv_decay" });
    addPage ("Sample", std::move (sample));

    auto cycling = std::make_unique<ScrollingPage> (p);
    cycling->page.addSection ({}, { "ab_mode", "ab_slice" });
    cycling->page.addSection ("PLAYBACK B", { "ab_scan", "ab_length", "ab_reverse", "ab_mono", "ab_interp", "ab_gain" });
    cycling->page.addSection ("PITCH B", { "ab_sample_pitch", "ab_penv_amt", "ab_fine", "ab_penv_decay", "ab_pitch" });
    addPage ("Cycling", std::move (cycling));

    auto motion = std::make_unique<ScrollingPage> (p);
    motion->page.addGroup ("ramp", "RAMP");
    motion->page.addRampReadout();
    addPage ("Motion", std::move (motion));
    addPage ("LFO 1", hammer::makeLfoPanel (p.apvts, "lfo1"));
    addPage ("LFO 2", hammer::makeLfoPanel (p.apvts, "lfo2"));
    addPage ("Amp", hammer::makeAmpPanel (p));
}

void ParamPanel::showGroup (int index)
{
    const int next = index - 1;
    if (next == selected) return;
    if (auto* current = pages[selected]) current->setVisible (false);
    selected = next;
    if (auto* page = pages[selected]) page->setVisible (true);
    resized();
}

void ParamPanel::paint (Graphics& g)
{
    hammer::drawRecess (g, getLocalBounds().toFloat(), hammer::skinFor (*this));
}

void ParamPanel::resized()
{
    if (auto* page = pages[selected])
        page->setBounds (getLocalBounds().reduced (8));
}
