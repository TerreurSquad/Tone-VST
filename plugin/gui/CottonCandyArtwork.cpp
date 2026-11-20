#include "CottonCandyArtwork.h"

#include "XtoneFaceplateData.h"

#include <cmath>

using namespace juce;

namespace hammer::cottonCandy
{
namespace
{
const Colour pearl { 0xfffffcfd };
const Colour blush { 0xffffd4e7 };
const Colour seam { 0xffc36b98 };
const Colour deepPink { 0xff963360 };

const Image& heart()
{
    static const auto image = ImageFileFormat::loadFrom (XtoneFaceplateData::TeddyBellyHeart_png,
                                                        XtoneFaceplateData::TeddyBellyHeart_pngSize);
    return image;
}

const Image& quiltTile()
{
    static const auto tile = []
    {
        const auto material = ImageFileFormat::loadFrom (XtoneFaceplateData::TeddyBellyQuilt_jpg,
                                                          XtoneFaceplateData::TeddyBellyQuilt_jpgSize);
        constexpr int size = 280;
        const auto patch = material.rescaled (size, size);
        Image mirrored (Image::RGB, size * 2, size * 2, false);
        Graphics g (mirrored);
        g.drawImageAt (patch, 0, 0);
        g.drawImageTransformed (patch, AffineTransform::scale (-1.0f, 1.0f).translated (size * 2, 0.0f));
        g.drawImageTransformed (patch, AffineTransform::scale (1.0f, -1.0f).translated (0.0f, size * 2));
        g.drawImageTransformed (patch, AffineTransform::scale (-1.0f, -1.0f).translated (size * 2, size * 2));
        return mirrored;
    }();
    return tile;
}

void drawSprite (Graphics& g, const Image& image, Rectangle<float> area, float rotation = 0.0f)
{
    const auto centre = area.getCentre();
    const auto transform = RectanglePlacement (RectanglePlacement::centred)
                               .getTransformToFit (image.getBounds().toFloat(), area)
                               .rotated (rotation, centre.x, centre.y);
    const float shadow = jlimit (1.5f, 5.0f, area.getHeight() * 0.035f);
    g.setColour (berry.withAlpha (0.24f));
    g.drawImageTransformed (image, transform.translated (0.0f, shadow), true);
    g.setColour (berry.withAlpha (0.18f));
    g.drawImageTransformed (image, transform.translated (0.0f, shadow * 0.45f), true);
    g.setOpacity (1.0f);
    g.drawImageTransformed (image, transform);
}

void drawPearlBead (Graphics& g, Point<float> centre, float radius)
{
    const auto bead = Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setColour (berry.withAlpha (0.36f));
    g.fillEllipse (bead.translated (0.0f, radius * 0.2f));
    auto light = ColourGradient (Colours::white, centre.translated (-radius * 0.32f, -radius * 0.36f),
                                 Colour (0xffb46086), centre.translated (radius * 0.65f, radius * 0.67f), true);
    light.addColour (0.5, Colour (0xffffe2eb));
    g.setGradientFill (light);
    g.fillEllipse (bead);
    g.setColour (pearl.withAlpha (0.65f));
    g.drawEllipse (bead.reduced (0.45f), 0.9f);
}

void drawSparkle (Graphics& g, Point<float> centre, float radius)
{
    const auto glow = Rectangle<float> (radius * 1.5f, radius * 1.5f).withCentre (centre);
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.68f), centre, Colours::white.withAlpha (0.0f),
                                       centre.translated (glow.getWidth() * 0.5f, 0.0f), true));
    g.fillEllipse (glow);

    Path flare;
    flare.startNewSubPath (0.0f, -1.0f);
    flare.quadraticTo (0.12f, -0.12f, 0.79f, 0.0f);
    flare.quadraticTo (0.12f, 0.12f, 0.0f, 1.0f);
    flare.quadraticTo (-0.12f, 0.12f, -0.79f, 0.0f);
    flare.quadraticTo (-0.12f, -0.12f, 0.0f, -1.0f);
    flare.closeSubPath();
    g.setColour (Colours::white.withAlpha (0.6f));
    g.fillPath (flare, AffineTransform::scale (radius * 0.62f).rotated (0.7854f).translated (centre));
    g.setColour (Colours::white);
    g.fillPath (flare, AffineTransform::scale (radius).translated (centre));
}

void drawPiping (Graphics& g, Rectangle<float> area, float radius)
{
    Path cord;
    cord.addRoundedRectangle (area, radius);
    g.setColour (deepPink.withAlpha (0.43f));
    g.strokePath (cord, PathStrokeType (5.5f), AffineTransform::translation (0.0f, 0.9f));
    g.setGradientFill (ColourGradient (pearl, area.getTopLeft(), Colour (0xfff4a8c7), area.getBottomLeft(), false));
    g.strokePath (cord, PathStrokeType (4.5f));

    constexpr float stitches[] { 3.0f, 2.3f };
    Path thread;
    PathStrokeType (1.15f, PathStrokeType::curved, PathStrokeType::rounded)
        .createDashedStroke (thread, cord, stitches, 2);
    g.setColour (Colours::white.withAlpha (0.87f));
    g.fillPath (thread, AffineTransform::translation (0.0f, -0.6f));
}

const Image& reference()
{
    static const auto image = ImageFileFormat::loadFrom (XtoneFaceplateData::TeddyBellyFaceplate_png,
                                                        XtoneFaceplateData::TeddyBellyFaceplate_pngSize);
    return image;
}

AffineTransform imageToCanvas()
{
    const auto& image = reference();
    return AffineTransform::scale ((float) canvasWidth / (float) image.getWidth(),
                                   (float) canvasHeight / (float) image.getHeight());
}

void referenceSection (Graphics& g, Rectangle<int> source, Rectangle<int> destination)
{
    const Graphics::ScopedSaveState state (g);
    if (! g.reduceClipRegion (destination)) return;
    const auto placement = RectanglePlacement (RectanglePlacement::stretchToFit)
                               .getTransformToFit (source.toFloat(), destination.toFloat());
    g.setOpacity (1.0f);
    g.drawImageTransformed (reference(), imageToCanvas().followedBy (placement));
}

void clearPrintedValue (Graphics& g, Rectangle<int> text, int cleanColumn)
{
    referenceSection (g, { cleanColumn, text.getY(), 2, text.getHeight() }, text);
}

void layQuilt (Graphics& g, Rectangle<int> area, int originX)
{
    g.setTiledImageFill (quiltTile(), originX, 123, 1.0f);
    g.fillRect (area);
    g.setColour (pearl.withAlpha (0.31f));
    g.fillRect (area);
}

// The knob is cut from the approved artwork. Coordinates trace its rose-metal silhouette.
const Image& referenceJewel()
{
    static const auto sprite = []
    {
        Image cutout (Image::ARGB, 232, 232, true);
        Graphics g (cutout);
        Path shape;
        shape.startNewSubPath (39.0f, 540.0f);
        shape.cubicTo (25.0f, 479.0f, 21.0f, 373.0f, 29.0f, 301.0f);
        shape.cubicTo (34.0f, 233.0f, 58.0f, 152.0f, 105.0f, 94.0f);
        shape.cubicTo (149.0f, 40.0f, 212.0f, 17.0f, 265.0f, 26.0f);
        shape.cubicTo (354.0f, 26.0f, 420.0f, 97.0f, 429.0f, 194.0f);
        shape.cubicTo (514.0f, 206.0f, 595.0f, 270.0f, 590.0f, 367.0f);
        shape.cubicTo (586.0f, 456.0f, 522.0f, 525.0f, 432.0f, 554.0f);
        shape.cubicTo (324.0f, 591.0f, 150.0f, 574.0f, 39.0f, 540.0f);
        shape.closeSubPath();
        // The crop origin is 685,263. Keep the original socket centre at the sprite centre.
        shape.applyTransform (AffineTransform::scale (1.0f / 3.0f).translated (24.0f, 3.0f));
        g.reduceClipRegion (shape);
        g.drawImageTransformed (reference(), imageToCanvas().translated (-661.0f, -260.0f));
        return cutout;
    }();
    return sprite;
}
} // namespace

void drawSurround (Graphics& g, Rectangle<int> bounds)
{
    static const auto fur = ImageFileFormat::loadFrom (XtoneFaceplateData::TeddyBellyPlush_jpg,
                                                      XtoneFaceplateData::TeddyBellyPlush_jpgSize);
    g.fillAll (deepPink);
    g.drawImage (fur, bounds.toFloat(), RectanglePlacement::fillDestination);
}

void drawFaceplate (Graphics& g, bool ratePage, bool sequencerPage, bool showDivisions)
{
    g.drawImageTransformed (reference(), imageToCanvas());

    // The labels in these three insets are driven by the live preset, sample and rate.
    clearPrintedValue (g, { 678, 59, 91, 19 }, 716);
    if (sequencerPage)
    {
        const Graphics::ScopedSaveState state (g);
        if (g.reduceClipRegion ({ 35, 109, 931, 501 }))
            drawSurround (g, { 0, 0, canvasWidth, canvasHeight });
        return;
    }

    clearPrintedValue (g, { 75, 137, 286, 19 }, 216);
    g.setColour (Colour (0xfffcf5fa));
    g.fillRect (67, 173, 520, 347);
    layQuilt (g, { 67, 522, 521, 16 }, 47);
    layQuilt (g, { 234, 557, 111, 19 }, 47);
    clearPrintedValue (g, { 358, 558, 31, 17 }, 389);

    if (! ratePage) return;

    layQuilt (g, { 632, 129, 82, 16 }, 616);
    clearPrintedValue (g, { 639, 152, 283, 51 }, 924);
    {
        const Graphics::ScopedSaveState state (g);
        Path well;
        well.addEllipse (640.0f, 240.0f, 276.0f, 272.0f);
        g.reduceClipRegion (well);
        g.reduceClipRegion ({ 635, 245, 284, 246 });
        layQuilt (g, { 635, 245, 284, 246 }, 616);
    }
    layQuilt (g, { 714, 491, 131, 28 }, 616);
    if (showDivisions)
    {
        clearPrintedValue (g, { 636, 537, 78, 15 }, 682);
        clearPrintedValue (g, { 759, 537, 142, 15 }, 858);
    }
    else
    {
        layQuilt (g, { 631, 520, 305, 38 }, 616);
    }
    layQuilt (g, { 731, 563, 136, 17 }, 616);
    clearPrintedValue (g, { 881, 563, 44, 17 }, 926);
}

bool usesPrintedCap (const Button& button)
{
    const int printed = button.getProperties().getWithDefault ("teddyPrinted", 0);
    return printed == (button.getToggleState() ? 2 : 1);
}

void drawPearl (Graphics& g, Rectangle<float> area, Colour top, Colour bottom, float radius)
{
    if (area.isEmpty()) return;
    const float depth = jmin (4.2f, area.getHeight() * 0.10f);
    g.setColour (berry.withAlpha (0.29f));
    g.fillRoundedRectangle (area.translated (0.0f, depth), radius);
    g.setGradientFill (ColourGradient (pearl, area.getTopLeft(), bottom.interpolatedWith (seam, 0.55f),
                                       area.getBottomLeft(), false));
    g.fillRoundedRectangle (area, radius);

    const auto face = area.reduced (1.35f);
    auto satin = ColourGradient (top, face.getTopLeft(), bottom, face.getBottomLeft(), false);
    satin.addColour (0.24, top.interpolatedWith (pearl, 0.72f));
    satin.addColour (0.77, bottom.interpolatedWith (top, 0.12f));
    g.setGradientFill (satin);
    g.fillRoundedRectangle (face, jmax (1.0f, radius - 1.0f));
    g.setColour (seam.withAlpha (0.8f));
    g.drawRoundedRectangle (area.reduced (0.5f), radius, 1.0f);
    g.setColour (Colours::white.withAlpha (0.78f));
    g.drawRoundedRectangle (area.reduced (2.0f), jmax (1.0f, radius - 1.5f), 0.9f);
}

void drawQuiltedPanel (Graphics& g, Rectangle<float> area)
{
    if (area.isEmpty()) return;
    constexpr float radius = 19.0f;
    drawPearl (g, area, pearl, blush, radius);
    {
        const Graphics::ScopedSaveState state (g);
        Path inset;
        inset.addRoundedRectangle (area.reduced (3.5f), radius - 3.0f);
        g.reduceClipRegion (inset);
        g.setTiledImageFill (quiltTile(), roundToInt (area.getX()), roundToInt (area.getY()), 1.0f);
        g.fillRect (area);
        g.fillAll (pearl.withAlpha (0.36f));
    }
    drawPiping (g, area.reduced (3.0f), radius - 2.0f);
}

void drawButton (Graphics& g, Rectangle<int> target, bool selected)
{
    const auto area = target.toFloat().reduced (0.5f);
    const auto top = selected ? Colour (0xffecfaff) : pearl;
    const auto bottom = selected ? Colour (0xffa5d5ff) : blush;
    drawPearl (g, area, top, bottom, 7.0f);
    if (selected)
    {
        g.setColour (Colour (0xff5e82a9));
        g.drawRoundedRectangle (area.reduced (0.5f), 7.0f, 1.0f);
    }
}

void drawSlider (Graphics& g, Rectangle<int> track, Rectangle<float> grip, bool enabled, bool focused)
{
    const auto area = track.toFloat().reduced (0.5f);
    const float radius = area.getHeight() * 0.5f;
    g.setGradientFill (ColourGradient (Colour (0xffda8db0), area.getTopLeft(), pearl, area.getBottomLeft(), false));
    g.fillRoundedRectangle (area, radius);
    {
        const Graphics::ScopedSaveState state (g);
        Path rail;
        rail.addRoundedRectangle (area, radius);
        g.reduceClipRegion (rail);
        g.setGradientFill (ColourGradient (enabled ? Colour (0xffff9fc8) : blush, area.getTopLeft(),
                                           enabled ? rose : seam, area.getBottomLeft(), false));
        g.fillRect (area.withRight (jlimit (area.getX(), area.getRight(), grip.getCentreX())));
    }
    g.setColour (focused && enabled ? rose : seam);
    g.drawRoundedRectangle (area, radius, 1.0f);
    drawPearlBead (g, grip.getCentre(), grip.getWidth() * 0.49f);
}

void drawKnob (Graphics& g, Rectangle<float> dial, float angle, float start, float end, bool focused, bool mainDial)
{
    const auto centre = dial.getCentre();
    const auto radius = dial.getWidth() * 0.5f;
    const auto mount = dial.expanded (radius * 0.045f);
    g.setColour (deepPink.withAlpha (0.27f));
    g.fillEllipse (mount.translated (0.0f, radius * 0.045f));
    auto rim = ColourGradient (pearl, mount.getTopLeft(), deepPink, mount.getBottomRight(), false);
    rim.addColour (0.38, Colour (0xffffc5df));
    rim.addColour (0.73, Colour (0xffde83af));
    g.setGradientFill (rim);
    g.fillEllipse (mount);
    const auto socket = dial.reduced (radius * 0.095f);
    auto satin = ColourGradient (Colour (0xffce85ad), socket.getTopLeft(), pearl, socket.getBottomRight(), false);
    satin.addColour (0.52, Colour (0xffffc9e1));
    g.setGradientFill (satin);
    g.fillEllipse (socket);
    g.setColour (pearl.withAlpha (0.85f));
    g.drawEllipse (socket, jlimit (1.0f, 2.0f, radius * 0.025f));

    const float arcRadius = radius * 1.12f;
    Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour (pearl);
    g.strokePath (track, PathStrokeType (3.6f, PathStrokeType::curved, PathStrokeType::rounded));
    g.setColour (focused ? rose : deepPink.withAlpha (0.67f));
    g.strokePath (track, PathStrokeType (1.35f, PathStrokeType::curved, PathStrokeType::rounded));

    if (mainDial)
    {
        // JUCE measures clockwise from the top. This is the angle of the original tip.
        const auto artworkAngle = std::atan2 (-79.0f, -67.0f);
        drawSprite (g, referenceJewel(), dial.expanded (radius * 0.075f), angle - artworkAngle);
    }
    else
    {
        drawSprite (g, heart(), dial.expanded (radius * 0.045f), angle - MathConstants<float>::pi);
    }
    const auto glint = centre.getPointOnCircumference (radius * 0.93f, start + (end - start) * 0.28f);
    drawSparkle (g, glint, jlimit (4.0f, 13.5f, radius * 0.13f));
}
} // namespace hammer::cottonCandy
