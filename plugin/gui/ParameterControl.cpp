#include "ParameterControl.h"

#include "HammerLookAndFeel.h"
#include "SampleView.h"

#include <utility>

using namespace juce;

namespace
{
String parameterHelp (String id)
{
    if (id.startsWith ("lfo1_") || id.startsWith ("lfo2_"))
        id = "lfo_" + id.substring (5);

    struct Tip { const char* id; const char* text; };
    constexpr Tip tips[] {
        { "midi_mode", "Choose whether MIDI notes change the retrigger pitch or select slices." },
        { "key_mode", "Relative offsets the selected rate from Root Note. Absolute tunes MIDI notes directly." },
        { "key_root", "MIDI note that plays the selected rate in Relative mode." },
        { "slice_base", "MIDI note that plays the selected slice in Slice mode." },
        { "transpose", "Shift the retrigger rate in semitones. An octave doubles the rate." },
        { "fine", "Fine-tune the retrigger rate in cents." },
        { "a4_ref", "Set the A4 reference for Absolute mode and Tune Snap." },
        { "pb_range", "Maximum MIDI pitch wheel shift in either direction, in semitones." },
        { "glide", "Time constant for sliding between MIDI notes." },
        { "glide_legato", "Glide only when MIDI notes overlap." },
        { "phase_reset", "Fire a hit and restart the clock when a new phrase begins." },
        { "vel_sens", "Set how strongly MIDI note velocity affects level." },
        { "snap_mode", "Base snaps the unmodulated pitch. All also snaps bends, ramps, and LFO sweeps." },
        { "snap_scale", "Choose which scale notes Tune Snap can use." },
        { "snap_root", "Root note of the Tune Snap scale." },
        { "trig_mode", "Cut fades the previous hit. Overlap lets hits play together." },
        { "declick", "Smooth hit edges and cuts. This also sets the minimum gate and rest fade time." },
        { "gate", "Start the hit's release after this percentage of the retrigger period. At 100%, play to the window end." },
        { "voices", "Maximum overlapping hits per unison lane, within the shared voice limit." },
        { "jitter", "Randomize the gap between hits while preserving the average rate." },
        { "hit_attack", "Time for each new hit to reach full level." },
        { "hit_decay", "Time for each hit to fall from full level to sustain." },
        { "hit_sustain", "Level each hit holds after decay." },
        { "hit_release", "Fade a hit when its gate closes, on a sequencer rest, or at its window end." },
        { "uni_voices", "Number of parallel retrigger clocks." },
        { "uni_detune", "Spread the unison rates above and below the center rate in cents." },
        { "uni_spread", "Spread the unison lanes across the stereo field." },
        { "uni_phase", "Give each unison clock a different starting phase." },
        { "slice", "Select the main slice. In Slice mode this offsets the MIDI selection." },
        { "interp", "Choose sample interpolation, from smooth Hermite to gritty Raw." },
        { "scan", "Move a shortened playback window through the slice." },
        { "length", "Set the playback window as a percentage of the slice." },
        { "reverse", "Play the selected window backward." },
        { "mono", "Mix the sample's left and right channels before unison panning." },
        { "s_pitch", "Shift sample playback speed in semitones while keeping hit timing." },
        { "s_fine", "Fine-tune sample playback speed in cents." },
        { "penv_amt", "Set the initial sample-pitch offset for each hit." },
        { "penv_decay", "Set how quickly each hit's pitch envelope returns to zero." },
        { "ab_mode", "Alternate between A and B on retrigger ticks. The sequencer takes priority." },
        { "ab_slice", "Choose the slice for B hits." },
        { "ab_scan", "Move B's shortened playback window through its slice." },
        { "ab_length", "Set the percentage of slice B to play." },
        { "ab_reverse", "Play B's window backward." },
        { "ab_mono", "Mix B's left and right channels before unison panning." },
        { "ab_interp", "Choose the interpolation used for B hits." },
        { "ab_gain", "Set the level of B hits relative to A." },
        { "ab_pitch", "Add a musical interval to B's sample pitch." },
        { "ab_sample_pitch", "Shift B's sample playback speed in semitones." },
        { "ab_fine", "Fine-tune B's playback speed in cents." },
        { "ab_penv_amt", "Set the initial sample-pitch offset for each B hit." },
        { "ab_penv_decay", "Set how quickly B's pitch envelope returns to zero." },
        { "ramp_on", "Sweep the retrigger rate after a MIDI note-on." },
        { "ramp_start", "Starting offset from the base rate, in octaves." },
        { "ramp_end", "Ending offset from the base rate, in octaves." },
        { "ramp_time", "Time from the start offset to the end offset." },
        { "ramp_curve", "Negative moves faster at first. Positive moves faster near the end. Zero is linear in octaves." },
        { "ramp_loop", "Hold the endpoint, jump back to the start, or sweep back and forth." },
        { "ramp_legato", "Restart the ramp even when MIDI notes overlap." },
        { "lfo_shape", "Choose the shape of the bipolar modulation signal." },
        { "lfo_sync", "Run at a frequency in Hz, follow host tempo, or lock to retrigger ticks." },
        { "lfo_hz", "LFO cycles per second in Free mode." },
        { "lfo_div", "Length of one LFO cycle at the host tempo." },
        { "lfo_ratio", "Number of ticks from the first unison lane in one LFO cycle." },
        { "lfo_phase", "Position in the waveform used when the LFO restarts." },
        { "lfo_retrig", "Restart this LFO at Start Phase on each MIDI note-on." },
        { "lfo_to_rate", "Sweep the retrigger pitch in semitones. Negative depth flips the modulation." },
        { "lfo_to_scan", "Move the playback window at each hit. Shorten Length to hear the effect." },
        { "lfo_to_gate", "Vary each hit's gate by this many percentage points." },
        { "lfo_to_pitch", "Modulate sample playback speed continuously in semitones." },
        { "env_a", "Time for the note to reach full level." },
        { "env_d", "Time for the note to fall toward the sustain level." },
        { "env_s", "Level to hold while a MIDI note stays on." },
        { "env_r", "Time for the sound to fade after note-off." },
        { "os", "Render at a higher internal rate to reduce aliasing. Higher settings use more CPU." },
        { "drive", "Boost the signal before clipping." },
        { "clip", "Choose soft saturation, hard clipping, or bypass." },
        { "out_gain", "Set the final level after drive and clipping." }
    };
    for (const auto& tip : tips)
        if (id == tip.id) return tip.text;
    return {};
}
} // namespace

ParameterControl::ParameterControl (RangedAudioParameter& parameter, State& state, String caption, bool stack, Style form)
    : style (form), stacked (stack || style == Style::knob)
{
    const auto name = parameter.getName (128);
    if (caption.isEmpty()) caption = name;
    const auto id = parameter.getParameterID();
    const auto tooltip = parameterHelp (id);

    if (dynamic_cast<AudioParameterBool*> (&parameter) != nullptr)
    {
        toggle = std::make_unique<ToggleButton> (caption);
        toggle->setTitle (name);
        toggle->setTooltip (tooltip);
        addAndMakeVisible (*toggle);
        toggleAttachment = std::make_unique<State::ButtonAttachment> (state, id, *toggle);
        return;
    }

    label.setText (caption, dontSendNotification);
    label.setFont (hammer::font (12.5f, Font::bold));
    label.setMinimumHorizontalScale (0.78f);
    label.setBorderSize (BorderSize<int> (0));
    label.setTooltip (tooltip);
    if (style == Style::knob) label.setJustificationType (Justification::centred);
    addAndMakeVisible (label);

    if (auto* p = dynamic_cast<AudioParameterChoice*> (&parameter))
    {
        choice = std::make_unique<ComboBox>();
        choice->addItemList (p->choices, 1);
        choice->setTitle (name);
        choice->setTooltip (tooltip);
        addAndMakeVisible (*choice);
        choiceAttachment = std::make_unique<State::ComboBoxAttachment> (state, id, *choice);
        return;
    }

    value = std::make_unique<Slider> (style == Style::knob ? Slider::RotaryHorizontalVerticalDrag : Slider::LinearBar,
                                      style == Style::knob ? Slider::TextBoxBelow : Slider::TextBoxLeft);
    value->setName (name);
    value->setTooltip (tooltip);
    value->setScrollWheelEnabled (false);
    addAndMakeVisible (*value);
    if (style != Style::slice)
    {
        valueAttachment = std::make_unique<State::SliderAttachment> (state, id, *value);
        return;
    }

    onlySlice = std::make_unique<Label> (String(), "1");
    onlySlice->setJustificationType (Justification::centred);
    onlySlice->setTitle (name);
    onlySlice->setTooltip (tooltip);
    addChildComponent (*onlySlice);
    sliceAttachment = std::make_unique<ParameterAttachment> (parameter, [this] (float index)
    {
        storedSlice = roundToInt (index);
        value->setValue (xt::wrapSliceIndex (storedSlice, sliceCount) + 1, dontSendNotification);
    });
    value->onDragStart = [this] { sliceGesture = true; sliceAttachment->beginGesture(); };
    value->onDragEnd = [this] { if (std::exchange (sliceGesture, false)) sliceAttachment->endGesture(); };
    value->onValueChange = [this]
    {
        if (sliceCount <= 1) return;
        const auto index = (float) value->getValue() - 1.0f;
        if (sliceGesture) sliceAttachment->setValueAsPartOfGesture (index);
        else sliceAttachment->setValueAsCompleteGesture (index);
    };
    setSliceCount (1);
    sliceAttachment->sendInitialUpdate();
}

ParameterControl::~ParameterControl()
{
    if (sliceGesture) sliceAttachment->endGesture();
}

void ParameterControl::setSliceCount (int count)
{
    if (sliceAttachment == nullptr || count == sliceCount) return;
    sliceCount = count;
    const bool multiple = count > 1;
    if (multiple) value->setRange (1.0, (double) count, 1.0);
    value->setValue (xt::wrapSliceIndex (storedSlice, count) + 1, dontSendNotification);
    value->setVisible (multiple);
    onlySlice->setVisible (! multiple);
}

void ParameterControl::lookAndFeelChanged()
{
    if (onlySlice == nullptr) return;
    const auto& colours = hammer::panelColours (*this);
    onlySlice->setColour (Label::backgroundColourId, colours.recess);
    onlySlice->setColour (Label::outlineColourId, colours.edge);
}

void ParameterControl::resized()
{
    auto r = getLocalBounds().reduced (4, 2);
    if (toggle != nullptr)
    {
        toggle->setBounds (r);
        return;
    }

    if (stacked)
        label.setBounds (r.removeFromTop (15));
    else
        label.setBounds (r.removeFromLeft (r.getWidth() * 47 / 100).withTrimmedRight (6));

    if (value != nullptr)
    {
        if (style == Style::knob) value->setTextBoxStyle (Slider::TextBoxBelow, false, jmin (92, r.getWidth()), 18);
        value->setBounds (r);
    }
    if (onlySlice != nullptr) onlySlice->setBounds (r);
    if (choice != nullptr) choice->setBounds (r);
}
