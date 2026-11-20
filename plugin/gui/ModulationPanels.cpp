#include "ModulationPanels.h"

#include "HammerLookAndFeel.h"
#include "ParameterControl.h"
#include "../PluginProcessor.h"

#include <array>
#include <cmath>

using namespace juce;

namespace
{
using State = AudioProcessorValueTreeState;

void drawPlot (Graphics& g, Rectangle<float> panel, Rectangle<float> plot, const hammer::PanelColours& colours)
{
    g.setColour (colours.recess);
    g.fillRoundedRectangle (panel, 6.0f);
    g.setColour (colours.edge);
    g.drawRoundedRectangle (panel.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (colours.edge.withAlpha (0.55f));
    for (int i = 0; i <= 4; ++i)
    {
        const auto fraction = (float) i / 4.0f;
        g.drawHorizontalLine (roundToInt (plot.getY() + plot.getHeight() * fraction), plot.getX(), plot.getRight());
        g.drawVerticalLine (roundToInt (plot.getX() + plot.getWidth() * fraction), plot.getY(), plot.getBottom());
    }
}

constexpr std::array<const char*, 4> envelopeIds { "env_a", "env_d", "env_s", "env_r" };

class EnvelopeGraph final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit EnvelopeGraph (XtoneProcessor& p) : processor (p)
    {
        setTitle ("Amplitude envelope graph");
        setTooltip ("Drag Attack, Decay, and Release horizontally. Drag Sustain vertically. Double-click a point to reset it.");
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            parameters[i] = p.apvts.getParameter (envelopeIds[i]);
            attachments[i] = std::make_unique<ParameterAttachment> (*parameters[i], [this] (float) { repaint(); });
        }
        startTimerHz (30);
    }

    ~EnvelopeGraph() override { endGesture(); }

    void paint (Graphics& g) override
    {
        const auto panel = getLocalBounds().toFloat().reduced (1.0f);
        const auto plot = plotArea();
        if (plot.isEmpty()) return;
        const auto& colours = hammer::panelColours (*this);
        drawPlot (g, panel, plot, colours);
        const auto accent = hammer::accentFor (*this);

        const auto h = handles();
        const auto start = plot.getBottomLeft();
        const auto end = Point<float> (h[3].x, plot.getBottom());
        const auto decayRelease = [] (float t) { return (std::pow (0.0001f, t) - 0.0001f) / 0.9999f; };

        Path curve;
        curve.startNewSubPath (start);
        curve.lineTo (h[0]);
        for (int i = 1; i <= 32; ++i)
        {
            const auto t = (float) i / 32.0f;
            curve.lineTo (jmap (t, h[0].x, h[1].x), jmap (decayRelease (t), h[1].y, h[0].y));
        }
        curve.lineTo (h[2]);
        for (int i = 1; i <= 32; ++i)
        {
            const auto t = (float) i / 32.0f;
            curve.lineTo (jmap (t, h[2].x, end.x), jmap (decayRelease (t), end.y, h[2].y));
        }
        auto area = curve;
        area.closeSubPath();
        g.setColour (accent.withAlpha (0.16f));
        g.fillPath (area);

        constexpr float dashes[] { 3.0f, 4.0f };
        g.setColour (colours.muted.withAlpha (0.6f));
        g.drawDashedLine ({ h[2].x, plot.getY(), h[2].x, plot.getBottom() }, dashes, 2, 1.0f);
        g.setColour (accent);
        g.strokePath (curve, PathStrokeType (2.0f));

        const auto stage = (int) liveStage - 1;
        if (isPositiveAndBelow (stage, 4))
        {
            const Graphics::ScopedSaveState saved (g);
            const auto left = stage == 0 ? start.x : h[(size_t) (stage - 1)].x;
            const auto right = h[(size_t) stage].x;
            g.reduceClipRegion (Rectangle<float> (left, plot.getY() - 2.0f, right - left, plot.getHeight() + 4.0f).getSmallestIntegerContainer());
            g.setColour (colours.text);
            g.strokePath (curve, PathStrokeType (2.6f));
        }

        constexpr std::array<Point<float>, 4> captionOffsets {{ { -11.0f, 10.0f }, { 0.0f, 13.0f },
                                                                { 9.0f, -12.0f }, { 0.0f, -13.0f } }};
        g.setFont (hammer::mono (10.0f, Font::bold));
        for (size_t i = 0; i < h.size(); ++i)
        {
            const auto point = Rectangle<float> (10.0f, 10.0f).withCentre (h[i]);
            g.setColour ((int) i == dragged ? colours.text : accent);
            g.fillEllipse (point);
            g.setColour (hammer::ink);
            g.drawEllipse (point.reduced (0.5f), 1.0f);
            g.setColour (colours.text);
            const auto caption = Rectangle<float> (15.0f, 14.0f).withCentre (h[i] + captionOffsets[i]);
            g.drawText (String::charToString ((juce_wchar) "ADSR"[i]), caption, Justification::centred);
        }

        const auto meter = Rectangle<float> (5.0f, plot.getHeight()).withPosition (plot.getRight() + 12.0f, plot.getY());
        g.setColour (colours.edge);
        g.fillRoundedRectangle (meter, 2.0f);
        const float level = jlimit (0.0f, 1.0f, liveLevel);
        g.setColour (accent);
        g.fillRoundedRectangle (meter.withTop (meter.getBottom() - meter.getHeight() * level), 2.0f);

        static constexpr const char* stageNames[] { "", "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
        g.setColour (colours.muted);
        g.setFont (hammer::mono (10.5f, Font::bold));
        auto heading = panel.reduced (10.0f, 3.0f).removeFromTop (15.0f);
        g.drawText ("AMP ENVELOPE", heading, Justification::centredLeft);
        g.drawText (stageNames[(size_t) liveStage], heading, Justification::centredRight);
    }

    void mouseMove (const MouseEvent& e) override
    {
        const int point = hitHandle (e.position);
        setMouseCursor (point < 0 ? MouseCursor::NormalCursor
                                 : point == 2 ? MouseCursor::UpDownResizeCursor : MouseCursor::LeftRightResizeCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) return;
        dragged = hitHandle (e.position);
        if (dragged < 0) return;

        origin = e.position;
        originalNormalised = parameters[(size_t) dragged]->getValue();
        attachments[(size_t) dragged]->beginGesture();
        repaint();
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragged < 0) return;
        const float delta = dragged == 2 ? (origin.y - e.position.y) / plotArea().getHeight()
                                         : (e.position.x - origin.x) / travel();
        const auto next = jlimit (0.0f, 1.0f, originalNormalised + delta);
        attachments[(size_t) dragged]->setValueAsPartOfGesture (parameters[(size_t) dragged]->convertFrom0to1 (next));
    }

    void mouseUp (const MouseEvent&) override { endGesture(); }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const int hit = hitHandle (e.position);
        if (hit < 0) return;
        endGesture();
        const auto* parameter = parameters[(size_t) hit];
        attachments[(size_t) hit]->setValueAsCompleteGesture (parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }

private:
    Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat().reduced (14.0f, 15.0f).withTrimmedTop (10.0f).withTrimmedRight (18.0f);
    }

    float travel() const { return jmax (1.0f, (plotArea().getWidth() - 14.0f * 3.0f - holdWidth()) / 3.0f); }
    float holdWidth() const { return plotArea().getWidth() * 0.21f; }

    std::array<Point<float>, 4> handles() const
    {
        const auto plot = plotArea();
        const auto xSpan = travel();
        const auto attack = plot.getX() + 14.0f + parameters[0]->getValue() * xSpan;
        const auto decay = attack + 14.0f + parameters[1]->getValue() * xSpan;
        const auto release = plot.getRight() - 14.0f - xSpan;
        const auto sustain = plot.getBottom() - plot.getHeight() * parameters[2]->getValue();
        return { Point<float> (attack, plot.getY()), { decay, sustain }, { release, sustain },
                 { release + 14.0f + parameters[3]->getValue() * xSpan, plot.getBottom() } };
    }

    int hitHandle (Point<float> point) const
    {
        const auto points = handles();
        float nearest = 14.0f;
        int hit = -1;
        for (size_t i = 0; i < points.size(); ++i)
        {
            const auto distance = point.getDistanceFrom (points[i]);
            if (distance < nearest)
            {
                nearest = distance;
                hit = (int) i;
            }
        }
        return hit;
    }

    void endGesture()
    {
        if (dragged < 0) return;
        attachments[(size_t) dragged]->endGesture();
        dragged = -1;
        repaint();
    }

    void timerCallback() override
    {
        if (! isVisible() || getWidth() == 0) return;
        const auto& telemetry = processor.telemetry();
        const auto level = telemetry.envelopeLevel.load (std::memory_order_relaxed);
        const auto stage = telemetry.envelopeStage.load (std::memory_order_relaxed);
        if (stage == liveStage && std::abs (level - liveLevel) < 0.002f) return;
        liveLevel = level;
        liveStage = stage;
        repaint();
    }

    XtoneProcessor& processor;
    std::array<RangedAudioParameter*, 4> parameters {};
    std::array<std::unique_ptr<ParameterAttachment>, 4> attachments;
    Point<float> origin;
    int dragged = -1;
    float originalNormalised = 0.0f;
    float liveLevel = 0.0f;
    xt::Adsr::Stage liveStage = xt::Adsr::Stage::Idle;
};

class OutputMeter final : public Component, private Timer
{
public:
    explicit OutputMeter (XtoneProcessor& p) : processor (p)
    {
        setTitle ("Stereo output peak meter");
        clip.setTitle ("Clear held output clipping");
        clip.setTooltip ("Lights when the output reaches 0 dBFS. Click to clear.");
        clip.onClick = [this] { processor.clearOutputClip(); updateClip(); };
        addAndMakeVisible (clip);
        updateClip();
        startTimerHz (30);
    }

    void resized() override { clip.setBounds (getLocalBounds().reduced (7, 3).removeFromTop (19).removeFromRight (46)); }

    void paint (Graphics& g) override
    {
        const auto& colours = hammer::panelColours (*this);
        const auto panel = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colours.recess);
        g.fillRoundedRectangle (panel, 5.0f);
        g.setColour (colours.edge);
        g.drawRoundedRectangle (panel, 5.0f, 1.0f);
        g.setColour (colours.muted);
        g.setFont (hammer::mono (10.5f, Font::bold));
        g.drawText ("OUTPUT PEAK  dBFS", 8, 4, getWidth() - 65, 17, Justification::centredLeft);

        const float left = 24.0f, right = (float) getWidth() - 13.0f;
        const auto xAt = [left, right] (float db) { return jmap (jlimit (-48.0f, 0.0f, db), -48.0f, 0.0f, left, right); };
        const auto accent = hammer::accentFor (*this);
        for (size_t channel = 0; channel < level.size(); ++channel)
        {
            const int y = 27 + (int) channel * 13;
            g.setColour (colours.muted);
            g.drawText (channel == 0 ? "L" : "R", 7, y - 2, 12, 12, Justification::centred);
            g.setColour (colours.edge);
            g.fillRoundedRectangle (Rectangle<float> (left, (float) y, right - left, 7.0f), 2.0f);
            g.setColour (accent);
            g.fillRoundedRectangle (Rectangle<float> (left, (float) y, xAt (level[channel]) - left, 7.0f), 2.0f);
            if (peak[channel] > -48.0f)
            {
                g.setColour (colours.text);
                g.fillRect (xAt (peak[channel]) - 1.0f, (float) y - 1.0f, 2.0f, 9.0f);
            }
        }

        g.setColour (colours.muted);
        g.setFont (hammer::mono (9.5f));
        for (const int db : { -48, -24, -12, -6, 0 })
            g.drawText (String (db), roundToInt (xAt ((float) db)) - 12, 52, 24, 14, Justification::centred);
    }

private:
    void updateClip()
    {
        const bool active = processor.hasOutputClipped();
        const auto skin = hammer::skinFor (*this);
        const auto warning = skin == xtp::EditorSkin::cottonCandy ? Colour (0xffa62642) : Colour (0xffff6556);
        const auto colour = active ? warning : hammer::panelColours (skin).muted;
        if (! clip.isColourSpecified (TextButton::textColourOffId) || clip.findColour (TextButton::textColourOffId) != colour)
            clip.setColour (TextButton::textColourOffId, colour);
    }

    void timerCallback() override
    {
        const double now = Time::getMillisecondCounterHiRes() * 0.001;
        const auto drop = (float) ((now - lastUpdate) * 30.0);
        lastUpdate = now;
        bool changed = false;
        for (size_t channel = 0; channel < level.size(); ++channel)
        {
            const auto next = Decibels::gainToDecibels (processor.takeOutputPeak ((int) channel), -48.0f);
            const auto displayed = jmax (next, level[channel] - drop);
            if (next > -48.0f && next >= peak[channel])
            {
                peak[channel] = next;
                holdUntil[channel] = now + 0.75;
                changed = true;
            }
            else if (now > holdUntil[channel] && peak[channel] > -48.0f)
            {
                peak[channel] = jmax (displayed, peak[channel] - drop);
                changed = true;
            }
            changed = changed || ! approximatelyEqual (displayed, level[channel]);
            level[channel] = displayed;
        }
        updateClip();
        if (changed) repaint();
    }

    XtoneProcessor& processor;
    TextButton clip { "CLIP" };
    std::array<float, 2> level { -48.0f, -48.0f }, peak { -48.0f, -48.0f };
    std::array<double, 2> holdUntil {};
    double lastUpdate = Time::getMillisecondCounterHiRes() * 0.001;
};

class AmpPanel final : public Component
{
public:
    explicit AmpPanel (XtoneProcessor& p) : graph (p), meter (p)
    {
        addAndMakeVisible (graph);
        for (size_t i = 0; i < controls.size(); ++i)
        {
            controls[i] = std::make_unique<ParameterControl> (*p.apvts.getParameter (envelopeIds[i]), p.apvts, String(), true);
            addAndMakeVisible (*controls[i]);
        }
        addAndMakeVisible (meter);
        constexpr const char* outputIds[] { "drive", "out_gain", "clip", "os" };
        for (size_t i = 0; i < output.size(); ++i)
        {
            using Style = ParameterControl::Style;
            output[i] = std::make_unique<ParameterControl> (*p.apvts.getParameter (outputIds[i]), p.apvts, String(), true,
                                                            i < 2 ? Style::knob : Style::bar);
            addAndMakeVisible (*output[i]);
        }
    }

    int minimumHeightForWidth (int width) const
    {
        return 12 + graphMinHeight + (int) (controls.size() / columnsForWidth (width)) * rowHeight
                  + 2 * sectionGap + meterHeight + knobHeight + rowHeight;
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced (6);
        const auto columns = columnsForWidth (getWidth());
        const int graphHeight = jlimit (graphMinHeight, 153,
                                        graphMinHeight + getHeight() - minimumHeightForWidth (getWidth()));
        graph.setBounds (content.removeFromTop (graphHeight));
        for (size_t first = 0; first < controls.size(); first += columns)
        {
            auto row = content.removeFromTop (rowHeight);
            for (size_t offset = 0; offset < columns; ++offset)
                controls[first + offset]->setBounds (row.removeFromLeft (row.getWidth() / (int) (columns - offset)));
        }
        content.removeFromTop (sectionGap);
        meter.setBounds (content.removeFromTop (meterHeight));
        content.removeFromTop (sectionGap);
        auto knobs = content.removeFromTop (knobHeight);
        output[0]->setBounds (knobs.removeFromLeft (knobs.getWidth() / 2));
        output[1]->setBounds (knobs);
        auto options = content.removeFromTop (rowHeight);
        output[2]->setBounds (options.removeFromLeft (options.getWidth() / 2));
        output[3]->setBounds (options);
    }

private:
    static constexpr int graphMinHeight = 90, rowHeight = 36, sectionGap = 5, meterHeight = 67, knobHeight = 106;
    static size_t columnsForWidth (int width) { return width >= 312 ? 4 : 2; }

    EnvelopeGraph graph;
    std::array<std::unique_ptr<ParameterControl>, 4> controls;
    OutputMeter meter;
    std::array<std::unique_ptr<ParameterControl>, 4> output;
};

class AmpPage final : public Component
{
public:
    explicit AmpPage (XtoneProcessor& p) : panel (p)
    {
        view.setViewedComponent (&panel, false);
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);
    }

    void resized() override
    {
        view.setBounds (getLocalBounds());
        const bool needsScrollBar = getHeight() < panel.minimumHeightForWidth (getWidth());
        const int width = jmax (0, getWidth() - (needsScrollBar ? view.getScrollBarThickness() : 0));
        panel.setSize (width, jmax (getHeight(), panel.minimumHeightForWidth (width)));
    }

private:
    AmpPanel panel;
    Viewport view;
};

float previewWave (int shape, float phase)
{
    switch (shape)
    {
        case 0: return std::sin (MathConstants<float>::twoPi * phase);
        case 1: return 1.0f - 4.0f * std::abs (phase - 0.5f);
        case 2: return 2.0f * phase - 1.0f;
        case 3: return 1.0f - 2.0f * phase;
        case 4: return phase < 0.5f ? 1.0f : -1.0f;
        default:
        {
            constexpr std::array<float, 9> steps { 0.55f, -0.35f, -0.7f, 0.2f, 0.85f, 0.4f, -0.58f, 0.18f, 0.55f };
            const float position = phase * 8.0f;
            const int index = jmin (7, (int) position);
            const float t = position - (float) index;
            return shape == 5 ? steps[(size_t) index]
                              : jmap (0.5f - 0.5f * std::cos (MathConstants<float>::pi * t),
                                      steps[(size_t) index], steps[(size_t) index + 1]);
        }
    }
}

class LfoGraph final : public Component, public SettableTooltipClient
{
public:
    LfoGraph (State& p, const String& prefix)
        : shape (*p.getParameter (prefix + "shape")), phase (*p.getParameter (prefix + "phase")),
          shapeAttachment (shape, [this] (float) { repaint(); }),
          phaseAttachment (phase, [this] (float) { repaint(); })
    {
        setTitle ("LFO waveform preview");
        setTooltip ("Drag horizontally to set Start Phase.");
        setMouseCursor (MouseCursor::LeftRightResizeCursor);
    }

    ~LfoGraph() override { if (dragging) phaseAttachment.endGesture(); }

    void paint (Graphics& g) override
    {
        auto panel = getLocalBounds().toFloat().reduced (1.0f);
        const auto plot = plotArea();
        if (plot.isEmpty()) return;
        const auto& colours = hammer::panelColours (*this);
        drawPlot (g, panel, plot, colours);
        const int wave = (int) shape.convertFrom0to1 (shape.getValue());
        const float centreY = plot.getCentreY();
        const float amplitude = plot.getHeight() * 0.42f;

        Path line;
        line.startNewSubPath (plot.getX(), centreY - amplitude * previewWave (wave, 0.0f));
        for (int i = 1; i <= 192; ++i)
        {
            const auto fraction = (float) i / 192.0f;
            line.lineTo (plot.getX() + plot.getWidth() * fraction, centreY - amplitude * previewWave (wave, fraction));
        }
        g.setColour (hammer::accentFor (*this));
        g.strokePath (line, PathStrokeType (2.0f));

        const auto position = plot.getX() + plot.getWidth() * phase.getValue();
        g.setColour (colours.text);
        g.drawLine (position, plot.getY(), position, plot.getBottom(), 1.4f);
        Path caret;
        caret.addTriangle (position - 4.0f, plot.getY() - 4.0f, position + 4.0f, plot.getY() - 4.0f, position, plot.getY() + 3.0f);
        g.fillPath (caret);
        g.setFont (hammer::font (11.5f, Font::bold));
        g.setColour (colours.muted);
        g.drawFittedText ("Waveform preview", panel.reduced (8.0f, 4.0f).removeFromTop (15.0f).toNearestInt(),
                          Justification::centredLeft, 1);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (! e.mods.isLeftButtonDown() || e.mods.isPopupMenu() || ! plotArea().expanded (0.0f, 5.0f).contains (e.position))
            return;
        phaseAttachment.beginGesture();
        dragging = true;
        movePhase (e.position.x);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging) movePhase (e.position.x);
    }

    void mouseUp (const MouseEvent&) override
    {
        if (! dragging) return;
        phaseAttachment.endGesture();
        dragging = false;
    }

private:
    Rectangle<float> plotArea() const { return getLocalBounds().toFloat().reduced (12.0f, 12.0f).withTrimmedTop (14.0f); }

    void movePhase (float x)
    {
        const auto plot = plotArea();
        const auto fraction = jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        phaseAttachment.setValueAsPartOfGesture (phase.convertFrom0to1 (fraction));
    }

    RangedAudioParameter& shape;
    RangedAudioParameter& phase;
    ParameterAttachment shapeAttachment, phaseAttachment;
    bool dragging = false;
};

class LfoPanel final : public Component
{
public:
    LfoPanel (State& state, const String& group)
        : graph (state, group + "_"),
          timingAttachment (*state.getParameter (group + "_sync"), [this] (float value) { showTiming ((int) value); })
    {
        addAndMakeVisible (graph);
        struct Control { const char* id; const char* caption; };
        constexpr Control definitions[] {
            { "shape", "Shape" }, { "sync", "Timing" }, { "hz", "Frequency" }, { "div", "Host Division" },
            { "ratio", "Rate Ratio" }, { "phase", "Start Phase" }, { "retrig", "Retrigger on Note" },
            { "to_rate", "Rate" }, { "to_scan", "Start Scan" }, { "to_gate", "Gate" }, { "to_pitch", "Sample Pitch" }
        };
        for (size_t i = 0; i < controls.size(); ++i)
        {
            auto* p = state.getParameter (group + "_" + definitions[i].id);
            controls[i] = std::make_unique<ParameterControl> (*p, state, definitions[i].caption, i <= phase);
            addAndMakeVisible (*controls[i]);
        }
        depthLabel.setText ("MODULATION DEPTH", dontSendNotification);
        depthLabel.setFont (hammer::mono (11.0f, Font::bold));
        addAndMakeVisible (depthLabel);
        timingAttachment.sendInitialUpdate();
    }

    void lookAndFeelChanged() override
    {
        depthLabel.setColour (Label::textColourId, hammer::panelColours (*this).muted);
    }

    void resized() override
    {
        auto content = getLocalBounds().reduced (6);
        if (content.isEmpty()) return;

        graph.setBounds (content.removeFromTop (jlimit (82, 190, content.getHeight() - 236)));
        content.removeFromTop (4);
        auto settings = content.removeFromTop (41);
        controls[shape]->setBounds (settings.removeFromLeft (settings.getWidth() / 2));
        controls[sync]->setBounds (settings);
        auto tempo = content.removeFromTop (41);
        controls[(size_t) timing]->setBounds (tempo.removeFromLeft (tempo.getWidth() / 2));
        controls[phase]->setBounds (tempo);
        controls[retrig]->setBounds (content.removeFromTop (28));
        depthLabel.setBounds (content.removeFromTop (17).withTrimmedLeft (4));
        for (size_t i = toRate; i < controls.size(); ++i)
            controls[i]->setBounds (content.removeFromTop (jmin (31, content.getHeight() / (int) (controls.size() - i))));
    }

private:
    enum { shape, sync, hz, division, ratio, phase, retrig, toRate, toScan, toGate, toPitch, numControls };

    void showTiming (int mode)
    {
        timing = hz + jlimit (0, 2, mode);
        for (int i = hz; i <= ratio; ++i)
            controls[(size_t) i]->setVisible (i == timing);
        resized();
    }

    LfoGraph graph;
    Label depthLabel;
    std::array<std::unique_ptr<ParameterControl>, numControls> controls;
    ParameterAttachment timingAttachment;
    int timing = hz;
};
} // namespace

std::unique_ptr<Component> hammer::makeAmpPanel (XtoneProcessor& processor)
{
    return std::make_unique<AmpPage> (processor);
}

std::unique_ptr<Component> hammer::makeLfoPanel (State& state, const String& groupId)
{
    return std::make_unique<LfoPanel> (state, groupId);
}
