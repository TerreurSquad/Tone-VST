#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "../SampleLoader.h"

#include <optional>

class XtoneProcessor;

/// Waveform with independent selected and playing slices.
/// Click selects a slice, Option-click auditions it, and the playback handles trim its window.
class WaveformView : public juce::Component, public juce::TooltipClient, private juce::Timer, private juce::ScrollBar::Listener
{
public:
    explicit WaveformView (XtoneProcessor&);
    ~WaveformView() override;

    juce::String getTooltip() override;
    void paint (juce::Graphics&) override;
    void lookAndFeelChanged() override;
    void setTeddyLayout (bool);
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct WindowSettings
    {
        float scan = 0.0f, length = 100.0f;
        bool reverse = false;
        bool operator== (const WindowSettings& other) const noexcept
        {
            return reverse == other.reverse && juce::approximatelyEqual (scan, other.scan)
                && juce::approximatelyEqual (length, other.length);
        }
    };

    struct TrimHandle { bool sliceB, startsPlayback; };

    struct TrimDrag
    {
        TrimHandle handle;
        xt::SliceWindow slice, initialWindow;
        double framesPerPixel;
        float anchorX, initialScan;
        bool leftEdge, gestureStarted = false;
    };

    void timerCallback() override;
    void updatePlotState();
    void scrollBarMoved (juce::ScrollBar*, double start) override;
    void updateAsset();
    void updateStatus();
    void chooseSample();
    void showSliceMenu();
    void editSliceEnvelope (xtp::SampleAsset::Ptr target, int slice);
    void startDetection();
    void setViewport (double start, double length);
    void zoomAt (double factor, double anchor);
    void paintSignal (juce::Graphics&, juce::Rectangle<int>) const;
    void paintTimeline (juce::Graphics&, juce::Rectangle<int>) const;
    juce::Rectangle<int> plotArea() const;
    juce::Rectangle<int> waveArea() const;
    bool usesTwoToolbarRows() const;
    bool teddyLayout = false;
    double xToFrame (float x) const;
    float frameToX (double frame) const;
    int markerAt (float x) const;
    int sliceAt (float x) const;
    int windowSlice (bool sliceB) const;
    xt::SliceWindow sliceBounds (int index) const;
    xt::SliceWindow playbackWindow (bool sliceB) const;
    float windowRailY (bool sliceB) const;
    juce::Rectangle<float> trimHandleBounds (const TrimHandle&, double frame) const;
    std::optional<TrimHandle> trimHandleAt (juce::Point<float>) const;
    void dragTrim (float x);
    void finishTrim();
    void selectSlice (int index);

    XtoneProcessor& proc;
    juce::TextButton loadSample { juce::String::fromUTF8 ("Load Sample…") }, normalize { "Normalize" };
    juce::TextButton transientButton { "Transients" }, sliceButton { juce::String::fromUTF8 ("Slices ▾") };
    juce::TextButton zoomOutButton { juce::String::fromUTF8 ("−") }, zoomInButton { "+" }, resetZoomButton { "Fit" };
    juce::Slider sensitivity { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label sampleName, sensitivityLabel, status;
    juce::ScrollBar scrollbar { false };
    std::unique_ptr<juce::FileChooser> chooser;
    SafePointer<juce::CallOutBox> envelopeEditor;

    struct PlotState
    {
        float position = -1.0f, auditionPosition = -1.0f;
        WindowSettings windowA, windowB;
        int selectedSlice = 0, cyclingSlice = -1, playingSlice = -1, auditionSlice = -1;
    };

    xtp::SampleAsset::Ptr asset;
    PlotState plotState;
    juce::String detectionMessage;
    bool sliceMenuOpen = false;
    bool selectOnRelease = false;
    bool auditionOnRelease = false;
    double visibleStart { 0.0 }, visibleLength { 0.0 };
    int dragIndex { -1 };
    xt::SliceModel dragSlices;
    std::optional<TrimDrag> trimDrag;
};
