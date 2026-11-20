#include "WaveformView.h"

#include "FaceplateArtwork.h"

#include "HammerLookAndFeel.h"

#include "../PluginProcessor.h"

#include <algorithm>
#include <array>
#include <utility>

using namespace juce;

namespace
{
struct WindowParameterIds { const char* scan; const char* length; const char* reverse; };

WindowParameterIds windowParameters (bool sliceB)
{
    return sliceB ? WindowParameterIds { "ab_scan", "ab_length", "ab_reverse" }
                  : WindowParameterIds { "scan", "length", "reverse" };
}

class SliceEnvelopeEditor final : public Component
{
public:
    SliceEnvelopeEditor (int slice, const xt::HitEnvelope& defaults, const std::optional<xt::HitEnvelope>& custom,
                         std::function<void (std::optional<xt::HitEnvelope>)> save)
    {
        heading.setText ("Slice " + String (slice + 1) + " ADSR", dontSendNotification);
        heading.setFont (hammer::font (17.0f, Font::bold));
        addAndMakeVisible (heading);
        customToggle.setToggleState (custom.has_value(), dontSendNotification);
        customToggle.setTooltip ("Override the Trigger ADSR for every hit that plays this slice.");
        customToggle.onClick = [this] { updateEnabled(); };
        addAndMakeVisible (customToggle);

        const auto env = custom.value_or (defaults);
        const std::array<double, 4> values { env.attackMs, env.decayMs, env.sustain * 100.0, env.releaseMs };
        constexpr const char* names[] { "Attack", "Decay", "Sustain", "Release" };
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            labels[i].setText (names[i], dontSendNotification);
            labels[i].setFont (hammer::font (14.0f));
            addAndMakeVisible (labels[i]);
            auto& slider = sliders[i];
            slider.setName (names[i]);
            slider.setSliderStyle (Slider::LinearHorizontal);
            slider.setTextBoxStyle (Slider::TextBoxRight, false, 73, 24);
            const bool sustain = i == 2;
            slider.setRange (0.0, sustain ? 100.0 : xt::HitEnvelope::kMaxTimeMs,
                             sustain ? 0.1 : xt::HitEnvelope::kTimeStepMs);
            if (! sustain) slider.setSkewFactorFromMidPoint (xt::HitEnvelope::kTimeMidpointMs);
            slider.setTextValueSuffix (sustain ? " %" : " ms");
            slider.setValue (values[i], dontSendNotification);
            addAndMakeVisible (slider);
        }
        updateEnabled();
        cancel.onClick = [this] { close(); };
        apply.onClick = [this, saveEnvelope = std::move (save)]
        {
            std::optional<xt::HitEnvelope> envelope;
            if (customToggle.getToggleState())
                envelope = xt::HitEnvelope { (float) sliders[0].getValue(), (float) sliders[1].getValue(),
                                             (float) (sliders[2].getValue() * 0.01), (float) sliders[3].getValue() };
            saveEnvelope (envelope);
            close();
        };
        addAndMakeVisible (cancel);
        addAndMakeVisible (apply);
        setSize (313, 216);
    }

    void paint (Graphics& g) override
    {
        g.setColour (hammer::panelColours (*this).rubber);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (9);
        heading.setBounds (area.removeFromTop (25));
        customToggle.setBounds (area.removeFromTop (25));
        area.removeFromTop (5);
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            auto row = area.removeFromTop (28);
            labels[i].setBounds (row.removeFromLeft (66));
            sliders[i].setBounds (row);
        }
        auto buttons = area.removeFromBottom (25);
        apply.setBounds (buttons.removeFromRight (74));
        buttons.removeFromRight (6);
        cancel.setBounds (buttons.removeFromRight (74));
    }

private:
    void updateEnabled()
    {
        for (auto& slider : sliders) slider.setEnabled (customToggle.getToggleState());
    }

    void close()
    {
        if (auto* box = findParentComponentOfClass<CallOutBox>()) box->dismiss();
    }

    Label heading;
    ToggleButton customToggle { "Use custom ADSR" };
    std::array<Label, 4> labels;
    std::array<Slider, 4> sliders;
    TextButton cancel { "Cancel" }, apply { "Apply" };
};
}

WaveformView::WaveformView (XtoneProcessor& p) : proc (p)
{
    setTitle ("Sample waveform");
    loadSample.setTooltip ("Open a WAV, AIFF, FLAC, Ogg, or MP3 sample. You can also drop it here.");
    normalize.setTooltip ("Raise the sample peak to full scale.");
    sensitivity.setTooltip ("Higher values detect quieter transients.");
    sensitivityLabel.setTooltip (sensitivity.getTooltip());
    sensitivity.setRange (0, 100, 1);
    sensitivity.setValue (50, dontSendNotification);
    sensitivity.setTextValueSuffix (" %");
    sensitivity.setTextBoxStyle (Slider::TextBoxRight, false, 42, 23);
    sensitivity.setName ("Sensitivity");
    sensitivityLabel.setText ("Sensitivity", dontSendNotification);
    sensitivityLabel.setFont (hammer::font (12.5f));
    sensitivityLabel.setBorderSize (BorderSize<int> (0));

    sampleName.setFont (hammer::font (16.0f, Font::bold));
    sampleName.setBorderSize (BorderSize<int> (0, 9, 0, 9));
    sampleName.setColour (Label::backgroundColourId, hammer::recess);
    sampleName.setColour (Label::outlineColourId, hammer::edge);
    status.setFont (hammer::font (12.5f));
    status.setColour (Label::textColourId, hammer::muted);
    status.setBorderSize (BorderSize<int> (0));

    transientButton.setTooltip ("Detect transients in the entire sample and replace the slice markers.");
    zoomOutButton.setTooltip ("Zoom out");
    zoomInButton.setTooltip ("Zoom in");
    resetZoomButton.setTooltip ("Show the entire sample");
    zoomOutButton.setTitle ("Zoom out on waveform");
    zoomInButton.setTitle ("Zoom in on waveform");
    resetZoomButton.setTitle ("Show the entire sample");
    sliceButton.setTooltip ("Edit the selected slice's ADSR, create evenly spaced slices, or clear markers.");
    scrollbar.setAutoHide (true);
    scrollbar.setName ("Scroll waveform");
    scrollbar.addListener (this);

    for (auto* button : { &loadSample, &normalize, &transientButton, &sliceButton, &zoomOutButton, &zoomInButton, &resetZoomButton })
        addAndMakeVisible (*button);
    for (Component* component : std::array<Component*, 5> { &sampleName, &sensitivity, &sensitivityLabel, &status, &scrollbar })
        addAndMakeVisible (*component);

    loadSample.onClick = [this] { chooseSample(); };
    normalize.onClick = [this] { proc.normalizeSample(); };
    transientButton.onClick = [this] { startDetection(); };
    sliceButton.onClick = [this] { showSliceMenu(); };
    zoomInButton.onClick = [this] { updateAsset(); zoomAt (2.0, 0.5); };
    zoomOutButton.onClick = [this] { updateAsset(); zoomAt (0.5, 0.5); };
    resetZoomButton.onClick = [this]
    {
        updateAsset();
        if (asset != nullptr && asset->audio != nullptr) setViewport (0.0, (double) asset->audio->frames());
    };

    timerCallback();
    startTimerHz (30);
}

WaveformView::~WaveformView()
{
    finishTrim();
    if (sliceMenuOpen) PopupMenu::dismissAllActiveMenus();
    if (envelopeEditor != nullptr) envelopeEditor->dismiss();
    scrollbar.removeListener (this);
}

void WaveformView::setTeddyLayout (bool next)
{
    if (teddyLayout == next) return;
    teddyLayout = next;
    sensitivityLabel.setVisible (! teddyLayout);
    for (auto* button : { &loadSample, &normalize, &transientButton, &sliceButton, &resetZoomButton,
                          &zoomInButton, &zoomOutButton })
        button->getProperties().set ("teddyPrinted", teddyLayout ? 1 : 0);
    lookAndFeelChanged();
    resized();
    repaint();
}

bool WaveformView::usesTwoToolbarRows() const { return getWidth() < 535; }

Rectangle<int> WaveformView::plotArea() const
{
    return getLocalBounds().reduced (13, 12).withTrimmedTop (41).withTrimmedBottom (usesTwoToolbarRows() ? 84 : 51);
}

Rectangle<int> WaveformView::waveArea() const
{
    if (teddyLayout) return { 29, 63, 522, 349 };
    return plotArea().withTrimmedBottom (33);
}

void WaveformView::resized()
{
    if (teddyLayout)
    {
        sampleName.setBounds (33, 24, 298, 28);
        loadSample.setBounds (340, 23, 108, 30);
        normalize.setBounds (452, 23, 97, 30);
        scrollbar.setBounds (30, 429, 520, 10);
        transientButton.setBounds (30, 445, 90, 27);
        sensitivity.setBounds (191, 445, 166, 27);
        sliceButton.setBounds (365, 445, 70, 27);
        resetZoomButton.setBounds (442, 445, 45, 27);
        zoomOutButton.setBounds (492, 445, 30, 27);
        zoomInButton.setBounds (524, 445, 28, 27);
        status.setBounds (31, 473, 519, 13);
        return;
    }
    auto content = getLocalBounds().reduced (13, 12);
    auto sample = content.removeFromTop (32);
    normalize.setBounds (sample.removeFromRight (104));
    sample.removeFromRight (5);
    loadSample.setBounds (sample.removeFromRight (108));
    sampleName.setBounds (sample.withTrimmedRight (8));

    auto plot = plotArea();
    scrollbar.setBounds (plot.removeFromBottom (11));
    status.setBounds (content.removeFromBottom (17));
    auto toolbar = content.removeFromBottom (usesTwoToolbarRows() ? 63 : 30);
    auto primary = toolbar.removeFromTop (30);
    auto actions = usesTwoToolbarRows() ? toolbar.removeFromBottom (28) : primary.removeFromRight (181);
    transientButton.setBounds (primary.removeFromLeft (92));
    primary.removeFromLeft (7);
    sensitivityLabel.setBounds (primary.removeFromLeft (86));
    sensitivity.setBounds (primary.withTrimmedRight (7));
    sliceButton.setBounds (actions.removeFromLeft (62));
    actions.removeFromLeft (8);
    resetZoomButton.setBounds (actions.removeFromLeft (49));
    actions.removeFromLeft (3);
    zoomOutButton.setBounds (actions.removeFromLeft (27));
    actions.removeFromLeft (3);
    zoomInButton.setBounds (actions.removeFromLeft (27));
}

void WaveformView::chooseSample()
{
    if (chooser != nullptr) return;
    chooser = std::make_unique<FileChooser> ("Load Sample", File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                          [self = SafePointer<WaveformView> (this)] (const FileChooser& dialog)
    {
        if (self == nullptr) return;
        const auto file = dialog.getResult();
        self->chooser.reset();
        if (file.existsAsFile()) self->proc.loadSampleFile (file);
    });
}

void WaveformView::showSliceMenu()
{
    updateAsset();
    if (asset == nullptr || sliceMenuOpen || envelopeEditor != nullptr || ! isShowing()) return;

    PopupMenu menu, equal;
    menu.setLookAndFeel (&getLookAndFeel());
    updatePlotState();
    const int selected = windowSlice (false);
    constexpr int editAdsr = 65;
    menu.addItem (editAdsr, "Edit Slice " + String (selected + 1) + String::fromUTF8 (" ADSR…"));
    menu.addSeparator();
    for (int first = 2; first <= 64;)
    {
        const int last = jmin (64, (first + 7) / 8 * 8);
        PopupMenu counts;
        for (int count = first; count <= last; ++count)
            counts.addItem (count, String (count) + " Slices");
        equal.addSubMenu (String (first) + String::fromUTF8 ("–") + String (last), counts);
        first = last + 1;
    }
    menu.addSubMenu ("Slice Evenly", equal);
    constexpr int clearMarkers = 1;
    menu.addItem (clearMarkers, "Clear All Markers", ! asset->slices.getMarkers().empty());
    sliceMenuOpen = true;
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&sliceButton),
                        [self = SafePointer<WaveformView> (this), target = asset, selected] (int choice)
    {
        if (self == nullptr) return;
        self->sliceMenuOpen = false;
        if (choice == 0) return;
        if (choice == editAdsr)
        {
            self->editSliceEnvelope (target, selected);
            return;
        }
        auto slices = target->slices;
        if (choice == clearMarkers) slices.clear();
        else slices.setEqual (choice);
        self->proc.setSlices (*target, std::move (slices));
    });
}

void WaveformView::editSliceEnvelope (xtp::SampleAsset::Ptr target, int slice)
{
    auto* editor = findParentComponentOfClass<AudioProcessorEditor>();
    if (editor == nullptr || ! isShowing() || proc.getCurrentAsset() != target) return;

    const auto raw = [this] (size_t i) { return proc.apvts.getRawParameterValue (xtp::triggerEnvelopeIds[i])->load(); };
    const xt::HitEnvelope defaults { raw (0), raw (1), raw (2) * 0.01f, raw (3) };
    const auto custom = target->slices.getEnvelopes()[(size_t) slice];
    auto content = std::make_unique<SliceEnvelopeEditor> (slice, defaults, custom,
        [self = SafePointer<WaveformView> (this), target, slice, custom] (std::optional<xt::HitEnvelope> envelope)
    {
        if (self == nullptr) return;
        if (! custom && ! envelope) return;
        if (custom && envelope && approximatelyEqual (custom->attackMs, envelope->attackMs)
            && approximatelyEqual (custom->decayMs, envelope->decayMs)
            && approximatelyEqual (custom->sustain, envelope->sustain)
            && approximatelyEqual (custom->releaseMs, envelope->releaseMs)) return;
        auto slices = target->slices;
        slices.setEnvelope (slice, envelope);
        if (! self->proc.setSlices (*target, std::move (slices)))
        {
            self->detectionMessage = "Sample or slices changed. Reopen Slice ADSR.";
            self->updateStatus();
        }
    });
    envelopeEditor = &CallOutBox::launchAsynchronously (std::move (content),
        editor->getLocalArea (&sliceButton, sliceButton.getLocalBounds()), editor);
}

void WaveformView::updateAsset()
{
    auto current = proc.getCurrentAsset();
    if (current == asset) return;

    finishTrim();
    const auto previousAudio = asset != nullptr ? asset->audio : nullptr;
    asset = std::move (current);
    dragIndex = -1;
    selectOnRelease = false;
    auditionOnRelease = false;
    if (asset == nullptr || asset->audio != previousAudio)
    {
        detectionMessage.clear();
        setViewport (0.0, asset != nullptr && asset->audio != nullptr ? (double) asset->audio->frames() : 0.0);
    }
    const bool hasAudio = asset != nullptr && asset->audio != nullptr;
    normalize.setEnabled (hasAudio && ! asset->normalized);
    sliceButton.setEnabled (hasAudio);
    sampleName.setText (hasAudio ? asset->name : "No Sample", dontSendNotification);
    sampleName.setTooltip (hasAudio ? asset->name : String());
    updateStatus();
    repaint();
}

void WaveformView::updateStatus()
{
    auto message = proc.getStatusMessage();
    if (message.isEmpty()) message = detectionMessage;
    if (message.isEmpty() && asset != nullptr)
    {
        if (asset->normalized) message = "Normalized";
        if (asset->fileBytes != nullptr)
        {
            if (message.isNotEmpty()) message << String::fromUTF8 ("  ·  ");
            message << "Embedded in project";
        }
    }
    status.setText (message, dontSendNotification);
    status.setTooltip (message);
}

void WaveformView::startDetection()
{
    updateAsset();
    if (asset == nullptr || asset->audio == nullptr) return;
    transientButton.setEnabled (false);
    sensitivity.setEnabled (false);
    detectionMessage = String::fromUTF8 ("Analyzing…");
    updateStatus();

    proc.detectTransients (asset, sensitivity.getValue() * 0.01,
                           [view = Component::SafePointer<WaveformView> (this)] (std::optional<int> count)
    {
        if (view == nullptr) return;
        view->updateAsset();
        view->transientButton.setEnabled (true);
        view->sensitivity.setEnabled (true);
        view->detectionMessage = ! count ? "Analysis discarded" : *count == 0 ? "No transients found" : "";
        view->updateStatus();
    });
}

void WaveformView::setViewport (double start, double length)
{
    const double frames = asset != nullptr && asset->audio != nullptr ? (double) asset->audio->frames() : 0.0;
    const double minimumLength = jmin (frames, 64.0);
    visibleLength = jlimit (minimumLength, frames, length);
    visibleStart = jlimit (0.0, frames - visibleLength, start);
    scrollbar.setRangeLimits (0.0, jmax (1.0, frames), dontSendNotification);
    scrollbar.setCurrentRange (visibleStart, visibleLength, dontSendNotification);
    scrollbar.setSingleStepSize (visibleLength * 0.1);
    scrollbar.setEnabled (visibleLength < frames);
    zoomOutButton.setEnabled (visibleLength < frames);
    resetZoomButton.setEnabled (visibleLength < frames);
    zoomInButton.setEnabled (visibleLength > minimumLength);
    repaint();
}

void WaveformView::zoomAt (double factor, double anchor)
{
    if (asset == nullptr || asset->audio == nullptr) return;
    const double frames = (double) asset->audio->frames();
    const double nextLength = jlimit (jmin (frames, 64.0), frames, visibleLength / factor);
    setViewport (visibleStart + anchor * (visibleLength - nextLength), nextLength);
}

void WaveformView::scrollBarMoved (ScrollBar*, double start)
{
    updateAsset();
    setViewport (start, visibleLength);
}

double WaveformView::xToFrame (float x) const
{
    const auto w = waveArea();
    const double fraction = jlimit (0.0, 1.0, (x - (double) w.getX()) / jmax (1, w.getWidth()));
    return visibleStart + fraction * visibleLength;
}

float WaveformView::frameToX (double frame) const
{
    const auto w = waveArea();
    return (float) (w.getX() + (frame - visibleStart) * w.getWidth() / visibleLength);
}

void WaveformView::paintSignal (Graphics& g, Rectangle<int> w) const
{
    const auto& audio = *asset->audio;
    const float* mono = audio.buffer.getReadPointer (2);
    const int frames = audio.buffer.getNumSamples();
    const double framesPerPixel = visibleLength / w.getWidth();
    const float midY = (float) w.getCentreY(), halfH = (float) w.getHeight() * 0.48f;
    g.setColour (hammer::accentFor (*this));

    if (framesPerPixel < 1.0)
    {
        Path trace;
        const int first = (int) std::floor (visibleStart);
        const int last = jmin (frames - 1, (int) std::ceil (visibleStart + visibleLength));
        trace.startNewSubPath (frameToX (first), midY - mono[first] * halfH);
        for (int frame = first + 1; frame <= last; ++frame)
            trace.lineTo (frameToX (frame), midY - mono[frame] * halfH);
        if (last == frames - 1) trace.lineTo (frameToX (frames), midY - mono[last] * halfH);
        g.strokePath (trace, PathStrokeType (1.0f));
        return;
    }

    constexpr int blockSize = xtp::SampleAsset::kPeakBlock;
    const auto* peaks = asset->peaks.get();
    for (int px = 0; px < w.getWidth(); ++px)
    {
        const int first = (int) std::floor (visibleStart + px * framesPerPixel);
        const int end = jmin (frames, (int) std::ceil (visibleStart + (px + 1) * framesPerPixel));
        float lo = 0.0f, hi = 0.0f;
        if (peaks != nullptr && framesPerPixel >= blockSize)
        {
            for (int block = first / blockSize; block * blockSize < end; ++block)
            {
                lo = jmin (lo, (*peaks)[(size_t) block * 2]);
                hi = jmax (hi, (*peaks)[(size_t) block * 2 + 1]);
            }
        }
        else
        {
            for (int frame = first; frame < end; ++frame)
            {
                lo = jmin (lo, mono[frame]);
                hi = jmax (hi, mono[frame]);
            }
        }
        g.drawVerticalLine (w.getX() + px, midY - hi * halfH, midY - lo * halfH + 1.0f);
    }
}

void WaveformView::paintTimeline (Graphics& g, Rectangle<int> w) const
{
    const auto& audio = *asset->audio;
    const int segments = teddyLayout ? 5 : jlimit (2, 5, w.getWidth() / 105);
    const double lengthSeconds = visibleLength / audio.sampleRate;
    g.setColour (hammer::panelColours (*this).muted);
    g.setFont (hammer::mono (11.0f));
    for (int i = 0; i <= segments; ++i)
    {
        const int labelWidth = w.getWidth() / segments;
        const double fraction = (double) i / (double) segments;
        const int x = w.getX() + roundToInt (fraction * w.getWidth());
        const int left = jlimit (w.getX(), w.getRight() - labelWidth, x - labelWidth / 2);
        const auto alignment = i == 0 ? Justification::centredLeft : i == segments ? Justification::centredRight : Justification::centred;
        const double time = visibleStart / audio.sampleRate + fraction * lengthSeconds;
        g.drawText (String (time, lengthSeconds < 0.02 ? 4 : 3) + " s", left, w.getBottom(), labelWidth, 20, alignment);
    }
}

void WaveformView::lookAndFeelChanged()
{
    const auto& colours = hammer::panelColours (*this);
    sampleName.setColour (Label::backgroundColourId, teddyLayout ? Colours::transparentBlack : colours.recess);
    sampleName.setColour (Label::outlineColourId, teddyLayout ? Colours::transparentBlack : colours.edge);
    sampleName.setFont (hammer::font (teddyLayout ? 14.0f : 16.0f, Font::bold));
    status.setColour (Label::textColourId, colours.muted);
}

void WaveformView::paint (Graphics& g)
{
    const auto skin = hammer::skinFor (*this);
    const auto accent = hammer::accentColour (skin);
    const auto& colours = hammer::panelColours (skin);
    const bool cotton = skin == xtp::EditorSkin::cottonCandy;
    const auto plotSurface = teddyLayout ? Colour (0xfffcf5fa)
                           : cotton ? Colour (0xfffffcff) : Colour (0xff101210);
    const auto handleSurface = cotton ? plotSurface : hammer::ink;
    if (! teddyLayout) hammer::artwork::drawWaveformPanel (g, getLocalBounds(), skin);
    const auto w = waveArea();
    g.setColour (plotSurface);
    g.fillRoundedRectangle (w.toFloat(), 5.0f);
    g.setColour (colours.edge);
    g.drawRoundedRectangle (w.toFloat().reduced (0.5f), 5.0f, 1.0f);
    if (visibleLength <= 0.0 || w.isEmpty())
    {
        g.setColour (colours.muted);
        g.setFont (hammer::font (14.0f));
        g.drawText ("Drop an audio file here", w, Justification::centred);
        return;
    }

    paintTimeline (g, w);
    const Graphics::ScopedSaveState state (g);
    if (! g.reduceClipRegion (w.reduced (1))) return;

    const auto& markers = dragIndex >= 0 ? dragSlices.getMarkers() : asset->slices.getMarkers();
    const int numSlices = (int) markers.size() + 1;
    const int selected = xt::wrapSliceIndex (plotState.selectedSlice, numSlices);
    const bool cycling = plotState.cyclingSlice >= 0;
    const int sliceB = xt::wrapSliceIndex (plotState.cyclingSlice, numSlices);
    const auto colourB = cotton ? Colour (0xff7947b8) : Colour (0xffc4a2ff);
    const auto auditionColour = cotton ? Colour (0xff286d9f) : Colour (0xff8cdbf4);
    const int playing = plotState.playingSlice;
    const bool isPlaying = isPositiveAndBelow (playing, numSlices);
    const auto frames = (double) asset->audio->frames();
    const auto xInPlot = [this, w] (double frame) { return jlimit ((float) w.getX(), (float) w.getRight(), frameToX (frame)); };
    const auto shade = [&] (int index, Colour colour)
    {
        const auto slice = sliceBounds (index);
        const auto x0 = xInPlot (slice.start), x1 = xInPlot (slice.end);
        g.setColour (colour.withAlpha (0.16f));
        g.fillRect (x0, (float) w.getY(), x1 - x0, (float) w.getHeight());
    };
    if (numSlices > 1) shade (selected, accent);
    if (cycling) shade (sliceB, colourB);
    if (isPlaying)
    {
        const auto slice = sliceBounds (playing);
        const float x0 = xInPlot (slice.start), x1 = xInPlot (slice.end);
        g.setColour (colours.text.withAlpha (0.9f));
        g.fillRect (x0, (float) w.getY() + 2.0f, x1 - x0, 2.0f);
    }

    g.setColour (colours.edge.withAlpha (0.5f));
    for (int i = 1; i <= 3; ++i)
        g.drawHorizontalLine (w.getY() + i * w.getHeight() / 4, (float) w.getX(), (float) w.getRight());

    paintSignal (g, w);

    const double visibleEnd = visibleStart + visibleLength;
    constexpr float dashes[] { 4.0f, 3.0f };
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto frame = (double) markers[i];
        if (frame < visibleStart || frame >= visibleEnd) continue;
        const float x = frameToX (frame);
        const auto handle = Rectangle<float> (12.0f, 15.0f).withCentre ({ x, (float) w.getBottom() - 10.0f });
        g.setColour (colours.text.withAlpha (0.72f));
        g.drawDashedLine ({ x, (float) w.getY(), x, handle.getY() }, dashes, 2, 1.0f);
        g.setColour ((int) i == dragIndex ? accent : colours.muted);
        g.fillRoundedRectangle (handle, 2.5f);
        g.setColour (handleSurface);
        for (int offset : { -2, 0, 2 })
            g.drawVerticalLine (roundToInt (x) + offset, handle.getY() + 4.0f, handle.getBottom() - 4.0f);
    }

    const auto drawWindow = [&] (bool isB, Colour colour)
    {
        const auto window = playbackWindow (isB);
        if (window.end <= visibleStart || window.start >= visibleEnd) return;
        const bool reverse = (isB ? plotState.windowB : plotState.windowA).reverse;
        const float y = windowRailY (isB);
        const float left = xInPlot (window.start), right = xInPlot (window.end);
        g.setColour (colour.withAlpha (0.1f));
        g.fillRect (left, (float) w.getY(), right - left, y - (float) w.getY());
        g.setColour (handleSurface.withAlpha (0.94f));
        g.fillRect (left, y - 3.5f, right - left, 7.0f);
        g.setColour (colour);
        g.fillRect (left, y - 1.75f, right - left, 3.5f);

        const auto edge = [&] (double frame, bool start)
        {
            const auto bounds = trimHandleBounds ({ isB, start }, frame);
            if (bounds.isEmpty()) return;
            const auto centre = bounds.getCentre();
            const bool activeHandle = trimDrag && trimDrag->handle.sliceB == isB && trimDrag->handle.startsPlayback == start;
            const auto handleColour = activeHandle ? colours.text : colour;
            g.setColour (handleColour);
            g.drawLine (centre.x, y, centre.x, centre.y, 2.0f);
            g.setColour (handleSurface);
            g.fillRoundedRectangle (bounds.reduced (1.0f, 0.5f), 2.5f);
            g.setColour (handleColour);
            if (start)
            {
                const float direction = reverse ? -1.0f : 1.0f;
                Path arrow;
                const float back = centre.x - direction * 3.0f;
                arrow.addTriangle (back, centre.y - 3.5f, centre.x + direction * 4.0f, centre.y, back, centre.y + 3.5f);
                g.fillPath (arrow);
            }
            else
            {
                g.fillRoundedRectangle (Rectangle<float> (4.0f, 7.0f).withCentre (centre), 1.0f);
            }
        };
        edge (window.start, ! reverse);
        edge (window.end, reverse);
    };
    drawWindow (false, accent);
    if (cycling) drawWindow (true, colourB);

    const auto playhead = [&] (float progress, Colour colour, bool dashed)
    {
        const double position = progress * frames;
        if (position < visibleStart || position >= visibleEnd) return;
        const float x = frameToX (position);
        const Line<float> line (x, (float) w.getY(), x, (float) w.getBottom());
        g.setColour (colour);
        if (dashed) g.drawDashedLine (line, dashes, 2, 1.5f);
        else g.drawLine (line, 1.0f);
    };
    playhead (plotState.position, colours.text, false);
    playhead (plotState.auditionPosition, auditionColour, true);

    auto readouts = w.reduced (8).removeFromTop (19);
    g.setFont (hammer::mono (12.0f, Font::bold));
    const auto badge = [&] (const String& text, Colour colour, int width)
    {
        const auto bounds = readouts.removeFromRight (width);
        readouts.removeFromRight (4);
        g.setColour (colours.recess.withAlpha (0.94f));
        g.fillRoundedRectangle (bounds.toFloat(), 3.0f);
        g.setColour (colour);
        g.drawText (text, bounds, Justification::centred);
    };
    if (plotState.auditionSlice >= 0) badge ("AUD " + String (plotState.auditionSlice + 1), auditionColour, 66);
    if (isPlaying && (cycling || playing != selected)) badge ("PLAY " + String (playing + 1), colours.text, 67);
    if (cycling) badge ("B " + String (sliceB + 1), colourB, 54);
    badge ((cycling ? "A " : "SLICE ") + String (selected + 1) + "/" + String (numSlices), accent, cycling ? 85 : 110);
}

int WaveformView::markerAt (float x) const
{
    const double frame = xToFrame (x);
    const double tolerance = 6.0 * visibleLength / waveArea().getWidth();
    const int index = asset->slices.findMarker (std::llround (frame), (int64_t) std::ceil (tolerance));
    if (index < 0) return -1;
    const auto marker = (double) asset->slices.getMarkers()[(size_t) index];
    return marker >= visibleStart && marker < visibleStart + visibleLength && std::abs (marker - frame) <= tolerance ? index : -1;
}

int WaveformView::sliceAt (float x) const
{
    const auto& markers = asset->slices.getMarkers();
    const auto frame = (int64_t) std::floor (xToFrame (x));
    return (int) std::distance (markers.begin(), std::upper_bound (markers.begin(), markers.end(), frame));
}

int WaveformView::windowSlice (bool sliceB) const
{
    return xt::wrapSliceIndex (sliceB ? plotState.cyclingSlice : plotState.selectedSlice, asset->slices.numSlices());
}

xt::SliceWindow WaveformView::sliceBounds (int index) const
{
    const auto& markers = dragIndex >= 0 ? dragSlices.getMarkers() : asset->slices.getMarkers();
    const auto i = (size_t) index;
    const auto start = i == 0 ? 0.0 : (double) markers[i - 1];
    const auto end = i == markers.size() ? (double) asset->audio->frames() : (double) markers[i];
    return { start, end };
}

xt::SliceWindow WaveformView::playbackWindow (bool sliceB) const
{
    const auto source = sliceBounds (windowSlice (sliceB));
    const auto& settings = sliceB ? plotState.windowB : plotState.windowA;
    return xt::sliceWindow (source.start, source.end, settings.scan * 0.01, settings.length * 0.01);
}

float WaveformView::windowRailY (bool sliceB) const
{
    return (float) waveArea().getBottom() - (sliceB ? 52.0f : 28.0f);
}

Rectangle<float> WaveformView::trimHandleBounds (const TrimHandle& handle, double frame) const
{
    const auto w = waveArea();
    if (frame < visibleStart || frame > visibleStart + visibleLength || w.getWidth() < 14) return {};
    const auto x = jlimit ((float) w.getX() + 7.0f, (float) w.getRight() - 7.0f, frameToX (frame));
    const auto y = windowRailY (handle.sliceB) + (handle.startsPlayback ? -7.0f : 4.0f);
    return Rectangle<float> (14.0f, 11.0f).withCentre ({ x, y });
}

std::optional<WaveformView::TrimHandle> WaveformView::trimHandleAt (Point<float> point) const
{
    std::optional<TrimHandle> nearest;
    float distance = 12.0f;
    for (const bool sliceB : { false, true })
    {
        if (sliceB && plotState.cyclingSlice < 0) break;
        const auto window = playbackWindow (sliceB);
        const bool reverse = (sliceB ? plotState.windowB : plotState.windowA).reverse;
        for (const bool start : { true, false })
        {
            const TrimHandle handle { sliceB, start };
            const auto bounds = trimHandleBounds (handle, start != reverse ? window.start : window.end);
            if (! bounds.isEmpty() && bounds.expanded (1.0f, 1.0f).contains (point))
            {
                const auto next = bounds.getCentre().getDistanceFrom (point);
                if (next < distance)
                {
                    nearest = handle;
                    distance = next;
                }
            }
        }
    }
    return nearest;
}

void WaveformView::dragTrim (float x)
{
    auto& drag = *trimDrag;
    const auto dx = x - drag.anchorX;
    if (! drag.gestureStarted && std::abs (dx) < 2.0f) return;

    const double span = drag.slice.end - drag.slice.start;
    const double minimumLength = jmax (1.0, span * 0.01);
    const double delta = dx * drag.framesPerPixel;
    auto window = drag.initialWindow;
    if (drag.leftEdge)
        window.start = jlimit (drag.slice.start, jmax (drag.slice.start, window.end - minimumLength), window.start + delta);
    else
        window.end = jlimit (jmin (drag.slice.end, window.start + minimumLength), drag.slice.end, window.end + delta);

    const double length = window.end - window.start;
    const auto scanValue = span - length > 1.0e-9 ? (float) (100.0 * (window.start - drag.slice.start) / (span - length))
                                                : drag.initialScan;
    const auto lengthValue = (float) (100.0 * length / span);

    const auto ids = windowParameters (drag.handle.sliceB);
    auto& scan = *proc.apvts.getParameter (ids.scan);
    auto& size = *proc.apvts.getParameter (ids.length);
    if (! drag.gestureStarted)
    {
        drag.gestureStarted = true;
        scan.beginChangeGesture();
        size.beginChangeGesture();
    }
    const auto set = [] (RangedAudioParameter& parameter, float value)
    {
        const auto next = parameter.convertTo0to1 (value);
        if (! approximatelyEqual (next, parameter.getValue())) parameter.setValueNotifyingHost (next);
    };
    set (scan, scanValue);
    set (size, lengthValue);
    updatePlotState();
}

void WaveformView::finishTrim()
{
    const auto drag = std::exchange (trimDrag, std::nullopt);
    if (! drag || ! drag->gestureStarted) return;
    const auto ids = windowParameters (drag->handle.sliceB);
    proc.apvts.getParameter (ids.scan)->endChangeGesture();
    proc.apvts.getParameter (ids.length)->endChangeGesture();
}

String WaveformView::getTooltip()
{
    const auto pos = getMouseXYRelative();
    if (asset == nullptr || visibleLength <= 0.0 || ! waveArea().contains (pos)) return {};
    if (const auto handle = trimHandleAt (pos.toFloat()))
        return handle->sliceB ? "Drag to trim the B playback window." : "Drag to trim the A playback window.";
    if (markerAt ((float) pos.x) >= 0)
        return "Drag to move this marker. Right-click to delete it.";
    return "Click to select a slice, Alt/Option-click to audition, or double-click to add a marker. Scroll to zoom.";
}

void WaveformView::mouseMove (const MouseEvent& e)
{
    if (visibleLength <= 0.0 || ! waveArea().contains (e.getPosition()))
    {
        setMouseCursor (MouseCursor::NormalCursor);
        return;
    }
    const bool resizable = ! e.mods.isAltDown() && (trimHandleAt (e.position).has_value() || markerAt (e.position.x) >= 0);
    setMouseCursor (resizable ? MouseCursor::LeftRightResizeCursor : MouseCursor::PointingHandCursor);
}

void WaveformView::mouseDown (const MouseEvent& e)
{
    updateAsset();
    finishTrim();
    selectOnRelease = false;
    auditionOnRelease = false;
    if (visibleLength <= 0.0 || ! waveArea().contains (e.getPosition())) return;
    selectOnRelease = e.mods.isLeftButtonDown() && ! e.mods.isPopupMenu();
    auditionOnRelease = selectOnRelease && e.mods.isAltDown();
    if (auditionOnRelease) return;
    updatePlotState();
    if (selectOnRelease)
    {
        if (const auto handle = trimHandleAt (e.position))
        {
            const auto& settings = handle->sliceB ? plotState.windowB : plotState.windowA;
            trimDrag = TrimDrag { *handle, sliceBounds (windowSlice (handle->sliceB)), playbackWindow (handle->sliceB),
                                  visibleLength / waveArea().getWidth(), e.position.x, settings.scan,
                                  handle->startsPlayback != settings.reverse };
            repaint (waveArea());
            return;
        }
    }
    const int index = markerAt (e.position.x);
    if (index < 0) return;
    if (e.mods.isPopupMenu())
    {
        auto slices = asset->slices;
        slices.remove (index);
        proc.setSlices (*asset, std::move (slices));
    }
    else if (selectOnRelease)
    {
        dragIndex = index;
        dragSlices = asset->slices;
    }
}

void WaveformView::mouseDrag (const MouseEvent& e)
{
    updateAsset();
    if (trimDrag)
    {
        dragTrim (e.position.x);
        return;
    }
    if (dragIndex < 0) return;
    dragSlices.move (dragIndex, std::llround (xToFrame (e.position.x)));
    repaint();
}

void WaveformView::mouseUp (const MouseEvent& e)
{
    updateAsset();
    const bool select = std::exchange (selectOnRelease, false) && waveArea().contains (e.getPosition())
                     && ! e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1;
    if (std::exchange (auditionOnRelease, false))
    {
        if (select)
        {
            updatePlotState();
            if (const auto handle = trimHandleAt (e.position))
                proc.auditionSlice (*asset, windowSlice (handle->sliceB), handle->sliceB);
            else
            {
                const int index = sliceAt (e.position.x);
                const bool useB = plotState.cyclingSlice >= 0 && index != windowSlice (false) && index == windowSlice (true);
                proc.auditionSlice (*asset, index, useB);
            }
        }
        return;
    }
    if (trimDrag)
    {
        if (select && ! trimDrag->gestureStarted) selectSlice (windowSlice (trimDrag->handle.sliceB));
        finishTrim();
        repaint (waveArea());
        return;
    }
    if (const int marker = std::exchange (dragIndex, -1); marker >= 0)
    {
        if (dragSlices.getMarkers() != asset->slices.getMarkers())
            proc.setSlices (*asset, dragSlices);
        else if (select)
            selectSlice (marker + 1);
        repaint();
    }
    else if (select)
    {
        selectSlice (sliceAt (e.position.x));
    }
}

void WaveformView::selectSlice (int index)
{
    auto* parameter = proc.apvts.getParameter ("slice");
    const float value = parameter->convertTo0to1 ((float) index);
    if (approximatelyEqual (value, parameter->getValue())) return;
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (value);
    parameter->endChangeGesture();
    plotState.selectedSlice = index;
    repaint (waveArea());
}

void WaveformView::mouseDoubleClick (const MouseEvent& e)
{
    updateAsset();
    if (visibleLength <= 0.0 || e.mods.isPopupMenu() || e.mods.isAltDown() || ! waveArea().contains (e.getPosition())) return;
    auto slices = asset->slices;
    if (slices.add (std::llround (xToFrame (e.position.x)))) proc.setSlices (*asset, std::move (slices));
}

void WaveformView::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    updateAsset();
    const auto w = waveArea();
    if (visibleLength <= 0.0 || dragIndex >= 0 || trimDrag || ! w.contains (e.getPosition())) return;
    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const double delta = wheel.deltaX != 0.0f ? wheel.deltaX : wheel.deltaY;
        setViewport (visibleStart - delta * visibleLength, visibleLength);
    }
    else if (wheel.deltaY != 0.0f)
    {
        const double anchor = (xToFrame (e.position.x) - visibleStart) / visibleLength;
        zoomAt (std::exp2 (4.0 * wheel.deltaY), anchor);
    }
}

void WaveformView::timerCallback()
{
    updateAsset();
    updateStatus();
    updatePlotState();
}

void WaveformView::updatePlotState()
{
    const auto& telemetry = proc.telemetry();
    const auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const auto window = [&raw] (bool isB)
    {
        const auto ids = windowParameters (isB);
        return WindowSettings { raw (ids.scan), raw (ids.length), raw (ids.reverse) != 0.0f };
    };
    const auto active = telemetry.active.load();
    const PlotState next { active ? telemetry.playPos.load() : -1.0f, telemetry.auditionPos.load(), window (false), window (true),
                           (int) raw ("slice"), raw ("ab_mode") != 0.0f ? (int) raw ("ab_slice") : -1,
                           active ? telemetry.slice.load() : -1, telemetry.auditionSlice.load() };
    if (next.selectedSlice != plotState.selectedSlice || next.playingSlice != plotState.playingSlice
        || next.cyclingSlice != plotState.cyclingSlice || next.auditionSlice != plotState.auditionSlice
        || next.windowA != plotState.windowA || next.windowB != plotState.windowB
        || ! approximatelyEqual (next.auditionPosition, plotState.auditionPosition)
        || ! approximatelyEqual (next.position, plotState.position))
    {
        plotState = next;
        repaint (waveArea());
    }
}
