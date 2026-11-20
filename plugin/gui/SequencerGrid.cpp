#include "SequencerGrid.h"

#include "HammerLookAndFeel.h"
#include "../PluginProcessor.h"

#include <algorithm>
#include <utility>

using namespace juce;

namespace
{
constexpr const char* rowNames[] { "On", "Tie", "Slice", "Pitch", "Velocity" };
constexpr int minimum[] { 0, 0, 0, -24, 0 };

void help (Component& control, SettableTooltipClient& tooltip, const String& title, const String& hint)
{
    control.setTitle (title);
    tooltip.setTooltip (hint);
}
} // namespace

SequencerGrid::SequencerGrid (XtoneProcessor& p)
    : proc (p), enabledAttachment (p.apvts, "seq_on", enabled), resetAttachment (p.apvts, "seq_reset", reset)
{
    // ComboBoxAttachment maps the integer parameter across the list of items.
    for (int i = 1; i <= (int) steps.size(); ++i)
        length.addItem (String (i) + " steps", i);
    lengthAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "seq_len", length);

    left.setButtonText (String::fromUTF8 ("←"));
    right.setButtonText (String::fromUTF8 ("→"));
    setWantsKeyboardFocus (true);
    setTitle ("Step sequencer grid");
    setDescription ("Use arrow keys to select a cell, Return to edit, and plus or minus to adjust.");
    for (Component* control : std::array<Component*, 7> { &enabled, &reset, &length, &clear, &random, &left, &right })
        addAndMakeVisible (*control);
    help (enabled, enabled, "Sequencer On", "Use one sequence step per retrigger tick. This takes priority over Cycling.");
    help (reset, reset, "Reset on Note", "Start at step 1 on the next hit after a MIDI note-on.");
    help (length, length, "Sequence Length", "Number of steps in the loop. Shortening it keeps the remaining steps.");
    help (clear, clear, "Clear sequence", "Turn every step into a rest and reset its values.");
    help (random, random, "Randomize sequence", "Generate a pattern of hits, rests, ties, and playback variations within the current length.");
    help (left, left, "Rotate left", "Move all steps in the loop one slot earlier, wrapping the first step to the end.");
    help (right, right, "Rotate right", "Move all steps in the loop one slot later, wrapping the last step to the front.");
    clear.onClick = [this]
    {
        xt::Sequence empty;
        for (auto& step : empty) step.on = false;
        proc.setSequence (empty);
        timerCallback();
    };
    random.onClick = [this] { randomize(); };
    left.onClick = [this] { rotate (-1); };
    right.onClick = [this] { rotate (1); };

    entry.setTitle ("Edit step value");
    entry.setFont (hammer::mono (13.0f));
    entry.setIndents (3, 2);
    entry.setInputRestrictions (4, "-0123456789");
    entry.onReturnKey = [this] { endEdit (true); grabKeyboardFocus(); };
    entry.onEscapeKey = [this] { endEdit (false); grabKeyboardFocus(); };
    entry.onFocusLost = [this] { endEdit (true); };
    addChildComponent (entry);
}

void SequencerGrid::visibilityChanged()
{
    if (isVisible())
    {
        timerCallback();
        startTimerHz (30);
    }
    else
    {
        endEdit (true);
        stopTimer();
    }
}

void SequencerGrid::timerCallback()
{
    steps = proc.getSequence();
    count = jlimit (1, (int) steps.size(), (int) proc.apvts.getRawParameterValue ("seq_len")->load());
    if (editing.step >= count) endEdit (false);
    if (focused.step >= count) focused = {};
    const int version = proc.getAssetVersion();
    if (assetVersion != version)
    {
        if (const auto asset = proc.getCurrentAsset()) slices = asset->slices.numSlices();
        assetVersion = version;
    }
    playhead = proc.telemetry().step.load (std::memory_order_relaxed);
    repaint();
}

void SequencerGrid::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    auto bar = area.removeFromTop (27);
    enabled.setBounds (bar.removeFromLeft (124));
    reset.setBounds (bar.removeFromLeft (127));
    length.setBounds (bar.removeFromLeft (94));
    right.setBounds (bar.removeFromRight (29));
    left.setBounds (bar.removeFromRight (29).withTrimmedRight (3));
    bar.removeFromRight (5);
    random.setBounds (bar.removeFromRight (84));
    bar.removeFromRight (5);
    clear.setBounds (bar.removeFromRight (61));

    area.removeFromTop (4);
    numbers = area.removeFromTop (18).withTrimmedLeft (66);
    rows = area.withTrimmedLeft (66);
    if (editing.step >= 0)
    {
        const auto cell = cellBounds (editing);
        entry.setBounds (cell.withSizeKeepingCentre (jmax (40, cell.getWidth()), cell.getHeight())
                             .constrainedWithin (getLocalBounds()));
    }
}

Rectangle<int> SequencerGrid::cellBounds (Cell cell) const
{
    const int columns = jmax (count, jmin (16, rows.getWidth() / 24));
    const int x = rows.getX() + cell.step * rows.getWidth() / columns;
    const int rightEdge = rows.getX() + (cell.step + 1) * rows.getWidth() / columns;
    const int y = rows.getY() + cell.row * rows.getHeight() / rowCount;
    const int bottom = rows.getY() + (cell.row + 1) * rows.getHeight() / rowCount;
    return { x, y, rightEdge - x, bottom - y };
}

void SequencerGrid::paint (Graphics& g)
{
    const auto skin = hammer::skinFor (*this);
    hammer::drawRecess (g, getLocalBounds().toFloat(), skin);
    const auto& colours = hammer::panelColours (skin);
    const auto playingText = skin == xtp::EditorSkin::cottonCandy ? colours.recess : hammer::ink;
    const auto accent = hammer::accentFor (*this);
    const auto font = hammer::mono (count > 24 ? 10.0f : 11.0f, Font::bold);
    g.setFont (font);
    for (int row = 0; row < rowCount; ++row)
    {
        const auto bounds = cellBounds ({ 0, row });
        g.setColour (colours.muted);
        g.drawText (rowNames[row], 14, bounds.getY(), 64, bounds.getHeight(), Justification::centredLeft);
    }

    for (int step = 0; step < count; ++step)
    {
        const auto col = cellBounds ({ step, 0 });
        const auto head = Rectangle<int> (col.getX(), numbers.getY(), col.getWidth(), numbers.getHeight()).reduced (1, 0);
        const auto playing = step == playhead && enabled.getToggleState();
        if (playing)
        {
            g.setColour (accent);
            g.fillRoundedRectangle (head.toFloat(), 3.0f);
        }
        g.setColour (playing ? playingText : colours.muted);
        g.drawText (String (step + 1), head, Justification::centred);

        for (int row = 0; row < rowCount; ++row)
        {
            const Cell cell { step, row };
            const auto bounds = cellBounds (cell).reduced (1, 1);
            const bool lit = row <= Tie && value (cell) != 0;
            g.setColour (lit ? accent.withAlpha (0.23f) : colours.rubber);
            g.fillRoundedRectangle (bounds.toFloat(), 2.0f);
            if (playing || (focused.step == step && focused.row == row))
            {
                g.setColour (playing ? accent.withAlpha (0.5f) : colours.muted);
                g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 2.0f, 1.0f);
            }
            if (lit)
            {
                g.setColour (accent);
                const auto dot = bounds.toFloat().withSizeKeepingCentre (7.0f, 7.0f);
                if (row == On) g.fillEllipse (dot);
                else g.drawEllipse (dot, 1.4f);
            }
            if (row >= Slice)
            {
                g.setColour (steps[(size_t) step].on && ! steps[(size_t) step].tie ? colours.text : colours.muted);
                g.drawFittedText (cellText (cell), bounds, Justification::centred, 1, 0.7f);
            }
        }
    }
}

SequencerGrid::Cell SequencerGrid::cellAt (Point<int> position) const
{
    if (! rows.contains (position)) return {};
    const int row = ((position.y - rows.getY() + 1) * rowCount - 1) / rows.getHeight();
    for (int step = 0; step < count; ++step)
        if (cellBounds ({ step, row }).contains (position)) return { step, row };
    return {};
}

String SequencerGrid::cellText (Cell cell) const
{
    const auto v = value (cell);
    if (cell.row == Slice && v == 0) return String::fromUTF8 ("–");
    if (cell.row == Pitch && v > 0) return "+" + String (v);
    return String (v);
}

int SequencerGrid::value (Cell cell) const
{
    const auto& step = steps[(size_t) cell.step];
    switch (cell.row)
    {
        case On: return (int) step.on;
        case Tie: return (int) step.tie;
        case Slice: return step.slice < 0 ? 0 : xt::wrapSliceIndex (step.slice, slices) + 1;
        case Pitch: return step.pitch;
        default: return roundToInt (step.velocity * 100.0f);
    }
}

int SequencerGrid::maxValue (int row) const
{
    constexpr int maxima[] { 1, 1, 128, 24, 100 };
    return row == Slice ? slices : maxima[row];
}

void SequencerGrid::change (Cell cell, int v)
{
    v = jlimit (minimum[cell.row], maxValue (cell.row), v);
    auto& step = steps[(size_t) cell.step];
    switch (cell.row)
    {
        case On: step.on = v != 0; break;
        case Tie: step.tie = v != 0; break;
        case Slice: step.slice = v - 1; break;
        case Pitch: step.pitch = v; break;
        default: step.velocity = (float) v / 100.0f; break;
    }
    proc.setSequenceStep (cell.step, step);
    repaint();
}

void SequencerGrid::mouseDown (const MouseEvent& e)
{
    if (! e.mods.isLeftButtonDown() || e.mods.isPopupMenu()) return;
    const auto cell = cellAt (e.getPosition());
    if (cell.step < 0) return;
    endEdit (true);
    grabKeyboardFocus();
    focused = dragging = cell;
    previousDragStep = cell.step;
    dragValue = value (cell);
    if (cell.row <= Tie)
    {
        dragValue = 1 - dragValue;
        change (cell, dragValue);
    }
    repaint();
}

void SequencerGrid::mouseDrag (const MouseEvent& e)
{
    if (dragging.step < 0 || dragging.step >= count) return;
    if (dragging.row <= Tie)
    {
        const auto cell = cellAt ({ e.x, cellBounds (dragging).getCentreY() });
        if (cell.step < 0 || cell.step == previousDragStep || previousDragStep >= count) return;
        const int direction = cell.step > previousDragStep ? 1 : -1;
        auto pattern = proc.getSequence();
        for (int step = previousDragStep + direction; step != cell.step + direction; step += direction)
        {
            auto& target = pattern[(size_t) step];
            (dragging.row == On ? target.on : target.tie) = dragValue != 0;
        }
        proc.setSequence (pattern);
        steps = pattern;
        previousDragStep = cell.step;
        repaint();
    }
    else
    {
        const int scale = dragging.row == Velocity ? 1 : 4;
        change (dragging, dragValue - e.getDistanceFromDragStartY() / scale);
    }
}

void SequencerGrid::mouseUp (const MouseEvent& e)
{
    if (dragging.step >= 0 && dragging.step < count && dragging.row >= Slice && ! e.mouseWasDraggedSinceMouseDown())
        startEdit (dragging);
    dragging = {};
}

void SequencerGrid::startEdit (Cell cell)
{
    editing = cell;
    resized();
    entry.setText (String (value (cell)), false);
    entry.setVisible (true);
    entry.grabKeyboardFocus();
    entry.selectAll();
}

void SequencerGrid::endEdit (bool commit)
{
    if (editing.step < 0) return;
    const auto cell = std::exchange (editing, Cell {});
    const auto input = entry.getText();
    if (commit && cell.step < count && input.containsAnyOf ("0123456789")) change (cell, input.getIntValue());
    entry.setVisible (false);
}

bool SequencerGrid::keyPressed (const KeyPress& key)
{
    if (focused.step < 0) focused = { 0, On };
    if (key == KeyPress::leftKey) focused.step = jmax (0, focused.step - 1);
    else if (key == KeyPress::rightKey) focused.step = jmin (count - 1, focused.step + 1);
    else if (key == KeyPress::upKey) focused.row = jmax (0, focused.row - 1);
    else if (key == KeyPress::downKey) focused.row = jmin ((int) rowCount - 1, focused.row + 1);
    else if (key == KeyPress::returnKey || key == KeyPress::spaceKey)
    {
        if (focused.row <= Tie) change (focused, 1 - value (focused));
        else startEdit (focused);
    }
    else if (key.getTextCharacter() == '+') change (focused, value (focused) + 1);
    else if (key.getTextCharacter() == '-') change (focused, value (focused) - 1);
    else return false;
    repaint();
    return true;
}

String SequencerGrid::cellHelp (Cell cell) const
{
    constexpr const char* hints[] {
        "Click or drag across steps to set hits or rests.",
        "Click or drag to hold the previous hit without retriggering. On must be enabled.",
        "Click to type or drag vertically. Enter 0 to follow the selected or MIDI slice.",
        "Click to type or drag vertically. Offsets sample pitch in semitones.",
        "Click to type or drag vertically. Sets this hit's level from 0 to 100%."
    };
    return "Step " + String (cell.step + 1) + String::fromUTF8 (" · ") + rowNames[cell.row] + ". " + hints[cell.row];
}

String SequencerGrid::getTooltip()
{
    const auto cell = cellAt (getMouseXYRelative());
    return cell.step >= 0 ? cellHelp (cell) : String();
}

void SequencerGrid::rotate (int direction)
{
    auto pattern = proc.getSequence();
    std::rotate (pattern.begin(), pattern.begin() + (direction < 0 ? 1 : count - 1), pattern.begin() + count);
    proc.setSequence (pattern);
    timerCallback();
}

void SequencerGrid::randomize()
{
    auto pattern = proc.getSequence();
    auto& rng = Random::getSystemRandom();
    for (int i = 0; i < count; ++i)
    {
        auto& step = pattern[(size_t) i];
        step.on = i == 0 || rng.nextFloat() < 0.8f;
        step.tie = i > 0 && step.on && pattern[(size_t) i - 1].on && rng.nextFloat() < 0.15f;
        step.slice = slices > 1 ? rng.nextInt (slices + 1) - 1 : -1;
        constexpr int offsets[] { -12, -7, 0, 0, 0, 7, 12 };
        step.pitch = offsets[rng.nextInt (7)];
        step.velocity = (float) (65 + rng.nextInt (36)) / 100.0f;
    }
    proc.setSequence (pattern);
    timerCallback();
}
