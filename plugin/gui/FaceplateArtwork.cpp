#include "FaceplateArtwork.h"
#include "CottonCandyArtwork.h"
#include "HammerLookAndFeel.h"

#include "XtoneFaceplateData.h"

#include <array>
#include <cmath>

using namespace juce;

namespace hammer::artwork
{
namespace
{
using xtp::EditorSkin;

const Image& mockup()
{
    static const auto image = ImageFileFormat::loadFrom (XtoneFaceplateData::HammerDrillFaceplate_png,
                                                        XtoneFaceplateData::HammerDrillFaceplate_pngSize);
    jassert (image.getWidth() == 1477 && image.getHeight() == 1065);
    return image;
}

// Coordinates in the embedded 1477 × 1065 mockup.
const Rectangle<int> samplePanel { 49, 126, 916, 761 };
const Rectangle<int> ratePanel { 972, 126, 461, 761 };
const Rectangle<int> presetPanel { 961, 40, 421, 70 };
const Rectangle<int> rateDisplay { 1004, 179, 398, 129 };
constexpr std::array<int, 4> footerSourceY { 887, 987, 1031, 1065 };
constexpr int footerSourceCorner = 108;

int footerCornerWidth (Rectangle<int> bounds) { return jmin (72, bounds.getWidth() / 4); }

std::array<int, 4> footerTargetY (Rectangle<int> bounds, int bodyBottom)
{
    // Match the vertical scale around the screws to the horizontal scale of the corners.
    const float scale = (float) footerCornerWidth (bounds) / (float) footerSourceCorner;
    const int bottom = bounds.getBottom();
    return { bodyBottom,
             bottom - roundToInt ((float) (footerSourceY[3] - footerSourceY[1]) * scale),
             bottom - roundToInt ((float) (footerSourceY[3] - footerSourceY[2]) * scale),
             bottom };
}

void blit (Graphics& g, const Image& image, Rectangle<int> source, Rectangle<int> target)
{
    if (source.isEmpty() || target.isEmpty()) return;
    g.drawImage (image, target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                 source.getX(), source.getY(), source.getWidth(), source.getHeight());
}

void blit (Graphics& g, Rectangle<int> source, Rectangle<int> target) { blit (g, mockup(), source, target); }

Image materialTile (const Image& image, Rectangle<int> source)
{
    // Mirror the photograph at the edges of the tile to avoid hard seams.
    const int w = source.getWidth(), h = source.getHeight();
    const auto patch = image.getClippedImage (source);
    Image tile (Image::RGB, w * 2, h * 2, true);
    Graphics g (tile);
    g.drawImageAt (patch, 0, 0);
    g.drawImageTransformed (patch, AffineTransform::scale (-1.0f, 1.0f).translated ((float) (w * 2), 0.0f));
    g.drawImageTransformed (patch, AffineTransform::scale (1.0f, -1.0f).translated (0.0f, (float) (h * 2)));
    g.drawImageTransformed (patch, AffineTransform::scale (-1.0f, -1.0f).translated ((float) (w * 2), (float) (h * 2)));
    return tile;
}

Image materialTile (Rectangle<int> source) { return materialTile (mockup(), source); }

const Image& blackPlastic()
{
    static const auto tile = materialTile ({ 108, 826, 330, 18 });
    return tile;
}

const Image& yellowPlastic()
{
    static const auto tile = materialTile ({ 1045, 851, 305, 15 });
    return tile;
}

std::array<int, 4> sliceAxis (int start, int end, int before, int after)
{
    const int extent = end - start;
    before = jmin (before, extent / 2);
    after = jmin (after, extent - before);
    return { start, start + before, end - after, end };
}

using SliceAxis = std::array<int, 4>;

void drawSlices (Graphics& g, const Image& image, const SliceAxis& sx, const SliceAxis& sy,
                 const SliceAxis& dx, const SliceAxis& dy, const Image* interior = nullptr)
{
    for (size_t row = 0; row < 3; ++row)
        for (size_t col = 0; col < 3; ++col)
        {
            const auto dest = Rectangle<int>::leftTopRightBottom (dx[col], dy[row], dx[col + 1], dy[row + 1]);
            if (row == 1 && col == 1 && interior != nullptr)
            {
                g.setTiledImageFill (*interior, dest.getX(), dest.getY(), 1.0f);
                g.fillRect (dest);
            }
            else
            {
                blit (g, image, Rectangle<int>::leftTopRightBottom (sx[col], sy[row], sx[col + 1], sy[row + 1]), dest);
            }
        }
}

void sliced (Graphics& g, const Image& image, Rectangle<int> source, Rectangle<int> target, BorderSize<int> crop,
             BorderSize<int> edge, const Image* interior = nullptr)
{
    if (target.isEmpty()) return;
    drawSlices (g, image,
                sliceAxis (source.getX(), source.getRight(), crop.getLeft(), crop.getRight()),
                sliceAxis (source.getY(), source.getBottom(), crop.getTop(), crop.getBottom()),
                sliceAxis (target.getX(), target.getRight(), edge.getLeft(), edge.getRight()),
                sliceAxis (target.getY(), target.getBottom(), edge.getTop(), edge.getBottom()), interior);
}

void blitWithScrew (Graphics& g, const Image& image, Rectangle<int> source, Rectangle<int> target,
                    Rectangle<int> screw)
{
    if (screw.isEmpty())
    {
        blit (g, image, source, target);
        return;
    }
    if (target.isEmpty()) return;
    jassert (source.contains (screw));

    const float xScale = (float) target.getWidth() / (float) source.getWidth();
    const float yScale = (float) target.getHeight() / (float) source.getHeight();
    const auto screwSize = roundToInt (jmin ((float) screw.getWidth() * xScale, (float) screw.getHeight() * yScale));
    const int screwX = target.getX() + roundToInt ((screw.toFloat().getCentreX() - (float) source.getX()) * xScale
                                                - (float) screwSize * 0.5f);
    const int screwY = target.getY() + roundToInt ((screw.toFloat().getCentreY() - (float) source.getY()) * yScale
                                                - (float) screwSize * 0.5f);

    // Stretch the housing around the fastener, and always draw the fastener into a square.
    drawSlices (g, image, { source.getX(), screw.getX(), screw.getRight(), source.getRight() },
                          { source.getY(), screw.getY(), screw.getBottom(), source.getBottom() },
                          { target.getX(), screwX, screwX + screwSize, target.getRight() },
                          { target.getY(), screwY, screwY + screwSize, target.getBottom() });
}

void panel (Graphics& g, Rectangle<int> source, Rectangle<int> target, BorderSize<int> crop,
            BorderSize<int> edge, const Image& interior)
{
    sliced (g, mockup(), source, target, crop, edge, &interior);
}

Image unlabelledButton (Rectangle<int> source, Rectangle<int> labelArea, int cleanX, int cleanWidth)
{
    Image face (Image::RGB, source.getWidth(), source.getHeight(), true);
    Graphics g (face);
    blit (g, source, face.getBounds());

    // Copy a clear strip from beside the lettering, preserving the original lighting down the cap.
    const auto clearFace = materialTile ({ source.getX() + cleanX, source.getY() + labelArea.getY(),
                                           cleanWidth, labelArea.getHeight() });
    g.setTiledImageFill (clearFace, labelArea.getX(), labelArea.getY(), 1.0f);
    g.fillRect (labelArea);
    return face;
}

Image sliderTrack (Rectangle<int> source, bool rightEnd)
{
    // The photographed grip interrupts each rail. Mirror its clear end to recover a full track.
    const auto end = mockup().getClippedImage (source);
    const int width = end.getWidth();
    Image track (Image::RGB, width * 2, end.getHeight(), true);
    Graphics g (track);
    g.drawImageAt (end, rightEnd ? width : 0, 0);
    g.drawImageTransformed (end, AffineTransform::scale (-1.0f, 1.0f)
                                   .translated ((float) (rightEnd ? width : 2 * width), 0.0f));
    return track;
}

struct PhotoLayout
{
    Rectangle<int> waveform, ratePanel, rateTexture, wordmark;
    Rectangle<int> presetDock, rateDisplay;
    int headerLeftEnd, headerRightStart;
    int footerStart, keysTop, keysBottom;
    Rectangle<int> upperLeftScrew, upperRightScrew, lowerLeftScrew, lowerRightScrew;
};

struct Photo
{
    Photo (const void* bytes, int size, PhotoLayout layout)
        : image (ImageFileFormat::loadFrom (bytes, (size_t) size)), geometry (layout),
          rateTexture (materialTile (image, geometry.rateTexture))
    {
        jassert (image.getWidth() == 1478 && image.getHeight() == 1064);
    }

    const Image image;
    const PhotoLayout geometry;
    const Image rateTexture;
};

const Photo* photoFor (EditorSkin skin)
{
    using namespace XtoneFaceplateData;
    switch (skin)
    {
        case EditorSkin::cottonCandy:
        case EditorSkin::hammerDrill:
            return nullptr;
        case EditorSkin::makita:
        {
            static const Photo makita (MakitaFaceplate_jpg, MakitaFaceplate_jpgSize,
                { { 45, 116, 852, 793 }, { 907, 116, 540, 793 }, { 976, 334, 115, 17 },
                  { 148, 21, 227, 65 }, { 812, 32, 567, 57 }, { 928, 165, 485, 106 },
                  809, 1383, 909, 930, 985, {}, { 1399, 36, 42, 44 }, { 36, 993, 44, 44 }, { 1400, 993, 44, 44 } });
            return &makita;
        }
        case EditorSkin::festool:
        {
            static const Photo festool (FestoolFaceplate_jpg, FestoolFaceplate_jpgSize,
                { { 43, 128, 853, 796 }, { 905, 128, 540, 796 }, { 952, 805, 100, 19 },
                  { 121, 41, 252, 62 }, { 809, 40, 572, 65 }, { 929, 178, 485, 112 },
                  806, 1382, 924, 941, 990,
                  { 53, 54, 39, 40 }, { 1386, 54, 40, 40 }, { 28, 991, 38, 39 }, { 1412, 991, 40, 39 } });
            return &festool;
        }
        case EditorSkin::hilti:
        {
            static const Photo hilti (HiltiFaceplate_jpg, HiltiFaceplate_jpgSize,
                { { 43, 131, 851, 795 }, { 906, 131, 540, 795 }, { 954, 805, 100, 19 },
                  { 116, 42, 257, 58 }, { 818, 42, 563, 61 }, { 928, 177, 485, 106 },
                  815, 1382, 926, 941, 993, { 56, 48, 42, 43 }, { 1387, 48, 42, 43 }, {}, {} });
            return &hilti;
        }
    }
    return nullptr;
}

struct ToolKnob
{
    ToolKnob (const void* bytes, int size)
        : sprite (ImageFileFormat::loadFrom (bytes, (size_t) size)), rotorBounds (opaqueBounds (sprite)) {}

    Image sprite;
    Rectangle<float> rotorBounds;

private:
    static Rectangle<float> opaqueBounds (const Image& image)
    {
        const Image::BitmapData pixels (image, Image::BitmapData::readOnly);
        int left = pixels.width, right = 0, top = pixels.height, bottom = 0;
        for (int y = 0; y < pixels.height; ++y)
            for (int x = 0; x < pixels.width; ++x)
                if (pixels.getPixelColour (x, y).getAlpha() >= 200)
                {
                    left = jmin (left, x);
                    right = jmax (right, x + 1);
                    top = jmin (top, y);
                    bottom = jmax (bottom, y + 1);
                }

        // Ignore the transparent padding when centring the physical rotor.
        const auto bounds = Rectangle<int>::leftTopRightBottom (left, top, right, bottom);
        return (bounds.isEmpty() ? image.getBounds() : bounds).toFloat();
    }
};

const ToolKnob& knobFor (EditorSkin skin)
{
    using namespace XtoneFaceplateData;
    switch (skin)
    {
        case EditorSkin::makita:
        {
            static const ToolKnob knob (MakitaKnob_png, MakitaKnob_pngSize);
            return knob;
        }
        case EditorSkin::festool:
        {
            static const ToolKnob knob (FestoolKnob_png, FestoolKnob_pngSize);
            return knob;
        }
        case EditorSkin::hilti:
        {
            static const ToolKnob knob (HiltiKnob_png, HiltiKnob_pngSize);
            return knob;
        }
        case EditorSkin::cottonCandy:
        case EditorSkin::hammerDrill:
            break;
    }

    static const ToolKnob knob (DeWaltKnob_png, DeWaltKnob_pngSize);
    return knob;
}

int photoHeaderWidth (const PhotoLayout& source, int top, int dockX)
{
    return jmin (dockX, source.headerLeftEnd * top / source.waveform.getY());
}

void drawPhotoFrame (Graphics& g, const Photo& photo, Rectangle<int> bounds, Rectangle<int> wave,
                     Rectangle<int> rate, Rectangle<int> dock)
{
    const auto& src = photo.geometry;
    const auto& image = photo.image;
    const int top = wave.getY();
    const int width = photoHeaderWidth (src, top, dock.getX());
    const int imageWidth = image.getWidth();
    const int sourceTop = src.waveform.getY();
    constexpr int corner = 108;
    const int cornerWidth = roundToInt ((float) corner * (float) width / (float) src.headerLeftEnd);
    blitWithScrew (g, image, { 0, 0, corner, sourceTop }, { 0, 0, cornerWidth, top }, src.upperLeftScrew);
    blit (g, image, { corner, 0, src.headerLeftEnd - corner, sourceTop },
          { cornerWidth, 0, width - cornerWidth, top });
    // Stretch the unlettered strip between the logo and the preset dock.
    blit (g, image, { src.headerLeftEnd - 4, 0, 4, sourceTop },
          { width, 0, dock.getRight() - width, top });
    blitWithScrew (g, image, { src.headerRightStart, 0, imageWidth - src.headerRightStart, sourceTop },
                   { dock.getRight(), 0, bounds.getRight() - dock.getRight(), top }, src.upperRightScrew);

    const int h = src.waveform.getHeight();
    blit (g, image, { 0, sourceTop, src.waveform.getX(), h }, { 0, top, wave.getX(), wave.getHeight() });
    blit (g, image, { src.waveform.getRight(), sourceTop, src.ratePanel.getX() - src.waveform.getRight(), h },
          { wave.getRight(), top, rate.getX() - wave.getRight(), wave.getHeight() });
    blit (g, image, { src.ratePanel.getRight(), sourceTop, imageWidth - src.ratePanel.getRight(), h },
          { rate.getRight(), top, bounds.getRight() - rate.getRight(), wave.getHeight() });

    // Keep the bumper and its hardware while reserving the recess for live keys.
    const auto keys = keyArea (bounds, wave.getBottom());
    constexpr int sourceInset = 64;
    const auto drawFooterRow = [&] (int y, int height, int targetY, int targetHeight, bool hasKeys)
    {
        blit (g, image, { 0, y, sourceInset, height }, { 0, targetY, keys.getX(), targetHeight });
        if (! hasKeys)
            blit (g, image, { sourceInset, y, imageWidth - 2 * sourceInset, height },
                  { keys.getX(), targetY, keys.getWidth(), targetHeight });
        blit (g, image, { imageWidth - sourceInset, y, sourceInset, height },
              { keys.getRight(), targetY, bounds.getRight() - keys.getRight(), targetHeight });
    };
    drawFooterRow (src.footerStart, src.keysTop - src.footerStart,
                   wave.getBottom(), keys.getY() - wave.getBottom(), false);
    drawFooterRow (src.keysTop, src.keysBottom - src.keysTop, keys.getY(), keys.getHeight(), true);
    if (src.lowerLeftScrew.isEmpty() && src.lowerRightScrew.isEmpty())
    {
        drawFooterRow (src.keysBottom, image.getHeight() - src.keysBottom,
                       keys.getBottom(), bounds.getBottom() - keys.getBottom(), false);
        return;
    }

    // The lower fasteners straddle the side edges of the key well in the photo.
    // Use broader slices below the keys so the full screw is in one slice.
    const int cornerInset = roundToInt ((float) corner * (float) keys.getX() / (float) sourceInset);
    const int bottom = keys.getBottom();
    const int height = bounds.getBottom() - bottom;
    const int sourceHeight = image.getHeight() - src.keysBottom;
    blitWithScrew (g, image, { 0, src.keysBottom, corner, sourceHeight }, { 0, bottom, cornerInset, height },
                   src.lowerLeftScrew);
    blit (g, image, { corner, src.keysBottom, imageWidth - 2 * corner, sourceHeight },
          { cornerInset, bottom, bounds.getWidth() - 2 * cornerInset, height });
    blitWithScrew (g, image, { imageWidth - corner, src.keysBottom, corner, sourceHeight },
                   { bounds.getRight() - cornerInset, bottom, cornerInset, height }, src.lowerRightScrew);
}
} // namespace

Rectangle<int> wordmarkBounds (int top, int dockX, EditorSkin skin)
{
    if (const auto* photo = photoFor (skin))
    {
        const auto& src = photo->geometry;
        return (src.wordmark.toFloat() * Point<float> ((float) photoHeaderWidth (src, top, dockX) / (float) src.headerLeftEnd,
                                                       (float) top / (float) src.waveform.getY())).getSmallestIntegerContainer();
    }

    const int leftCorner = jmin (roundToInt (104.0f * (float) top / 126.0f), dockX / 2);
    const int width = jmin (dockX - leftCorner, 847 * top / 126);
    const auto logo = Rectangle<float> (21.0f, 40.0f, 298.0f, 63.0f)
                    * Point<float> ((float) width / 847.0f, (float) top / 126.0f);
    return logo.getSmallestIntegerContainer().translated (leftCorner, 0);
}

void drawFrame (Graphics& g, Rectangle<int> bounds, Rectangle<int> wave, Rectangle<int> rate, Rectangle<int> dock,
                EditorSkin skin)
{
    g.fillAll (Colour (0xff141413));
    if (wave.isEmpty() || rate.isEmpty() || dock.isEmpty()) return;

    if (const auto* photo = photoFor (skin))
    {
        drawPhotoFrame (g, *photo, bounds, wave, rate, dock);
        return;
    }

    const int top = wave.getY();
    const int leftCorner = jmin (roundToInt (104.0f * (float) top / 126.0f), dock.getX() / 2);
    const int brandingWidth = jmin (dock.getX() - leftCorner, 847 * top / 126);
    const int brandingEnd = leftCorner + brandingWidth;
    blitWithScrew (g, mockup(), { 0, 0, 104, 126 }, { 0, 0, leftCorner, top }, { 50, 51, 43, 43 });
    blit (g, { 104, 0, 847, 126 }, { leftCorner, 0, brandingWidth, top });
    // Extend the unlettered housing next to the logo while retaining its top and bottom rails.
    static const auto blankHeader = materialTile ({ 103, 0, 20, 126 });
    g.setFillType (FillType (blankHeader, AffineTransform::scale (0.67f, (float) top / 126.0f)
                                                           .translated ((float) brandingEnd, 0.0f)));
    g.fillRect (brandingEnd, 0, dock.getRight() - brandingEnd, top);
    blitWithScrew (g, mockup(), { 1386, 0, 91, 126 }, { dock.getRight(), 0, bounds.getRight() - dock.getRight(), top },
                   { 1388, 51, 43, 43 });

    const int midHeight = wave.getHeight();
    blit (g, { 0, 126, samplePanel.getX(), 761 }, { 0, top, wave.getX(), midHeight });
    blit (g, { samplePanel.getRight(), 126, ratePanel.getX() - samplePanel.getRight(), 761 },
          { wave.getRight(), top, rate.getX() - wave.getRight(), midHeight });
    blit (g, { ratePanel.getRight(), 126, 1477 - ratePanel.getRight(), 761 },
          { rate.getRight(), top, bounds.getRight() - rate.getRight(), midHeight });

    // Take the entire lower bumper, including the screws and the underside vents.
    // The live key well covers the original keys inside this strip.
    const int footerCorner = footerCornerWidth (bounds);
    const auto footerY = footerTargetY (bounds, wave.getBottom());
    for (size_t row = 0; row < 3; ++row)
    {
        const int y = footerSourceY[row];
        const int h = footerSourceY[row + 1] - y;
        const int targetY = footerY[row];
        const int targetHeight = footerY[row + 1] - targetY;
        const auto leftScrew = row == 1 ? Rectangle<int> (28, 988, 42, 42) : Rectangle<int>();
        const auto rightScrew = row == 1 ? Rectangle<int> (1409, 988, 43, 42) : Rectangle<int>();
        blitWithScrew (g, mockup(), { 0, y, footerSourceCorner, h }, { 0, targetY, footerCorner, targetHeight }, leftScrew);
        blit (g, { 108, y, 1261, h }, { footerCorner, targetY, bounds.getWidth() - footerCorner * 2, targetHeight });
        blitWithScrew (g, mockup(), { 1369, y, footerSourceCorner, h },
                       { bounds.getRight() - footerCorner, targetY, footerCorner, targetHeight }, rightScrew);
    }
}

void drawWaveformPanel (Graphics& g, Rectangle<int> target, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawQuiltedPanel (g, target.toFloat());
        return;
    }
    if (const auto* photo = photoFor (skin))
    {
        sliced (g, photo->image, photo->geometry.waveform, target, { 18, 16, 15, 16 },
                { 10, 12, 10, 12 }, &blackPlastic());
        return;
    }
    panel (g, samplePanel, target, { 49, 34, 44, 26 }, { 14, 15, 12, 14 }, blackPlastic());
}

void drawRatePanel (Graphics& g, Rectangle<int> target, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawQuiltedPanel (g, target.toFloat());
        return;
    }
    if (const auto* photo = photoFor (skin))
    {
        sliced (g, photo->image, photo->geometry.ratePanel, target, { 12, 16, 12, 16 },
                { 9, 12, 9, 12 }, &photo->rateTexture);
        return;
    }
    panel (g, ratePanel, target, { 20, 29, 40, 29 }, { 11, 14, 15, 14 }, yellowPlastic());
}

void drawPresetDock (Graphics& g, Rectangle<int> target, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawPearl (g, target.toFloat(), Colours::white, Colour (0xfff095c2), 14.0f);
        return;
    }
    static const auto brushedMetal = materialTile ({ 981, 89, 69, 7 });
    if (const auto* photo = photoFor (skin))
    {
        sliced (g, photo->image, photo->geometry.presetDock, target, { 6, 9, 7, 9 },
                { 4, 6, 5, 5 }, &brushedMetal);
        return;
    }
    panel (g, presetPanel, target, { 10, 14, 9, 9 }, { 6, 8, 6, 7 }, brushedMetal);
}

void drawRateDisplay (Graphics& g, Rectangle<int> target, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawPearl (g, target.toFloat(), Colour (0xfff9fdff), Colour (0xffd9efff), 11.0f);
        return;
    }
    static const auto glass = materialTile ({ 730, 551, 126, 105 });
    if (const auto* photo = photoFor (skin))
    {
        sliced (g, photo->image, photo->geometry.rateDisplay, target, { 10, 13, 10, 13 },
                { 7, 8, 7, 8 }, &glass);
        return;
    }
    panel (g, rateDisplay, target, { 11, 14, 10, 15 }, { 8, 8, 7, 8 }, glass);
}

void drawButton (Graphics& g, Rectangle<int> target, bool selected, bool key, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawButton (g, target, selected);
        return;
    }
    if (selected && skin != EditorSkin::hammerDrill)
    {
        const auto face = target.toFloat().reduced (0.5f);
        const auto tint = skin == EditorSkin::hilti ? Colour (0xffc92016) : accentColour (skin);
        g.setGradientFill (ColourGradient (tint.brighter (0.18f), face.getTopLeft(),
                                           tint.darker (0.22f), face.getBottomLeft(), false));
        g.fillRoundedRectangle (face, 4.0f);
        g.setColour (Colours::white.withAlpha (0.28f));
        g.drawRoundedRectangle (face.reduced (0.5f), 3.5f, 1.0f);
        return;
    }

    static const auto yellowKey = unlabelledButton ({ 78, 919, 102, 68 }, { 15, 15, 72, 38 }, 11, 9);
    static const auto blackKey = unlabelledButton ({ 181, 919, 91, 68 }, { 14, 15, 63, 38 }, 10, 10);
    static const auto blackAction = unlabelledButton ({ 591, 193, 170, 51 }, { 13, 8, 147, 36 }, 9, 8);
    const auto& cap = selected ? yellowKey : key ? blackKey : blackAction;
    const BorderSize<int> sourceEdge = selected || key ? BorderSize<int> { 12, 10, 10, 10 }
                                                        : BorderSize<int> { 9, 8, 9, 8 };
    sliced (g, cap, cap.getBounds(), target, sourceEdge, { 5, 5, 6, 5 });
}

void drawSlider (Graphics& g, Rectangle<int> track, Rectangle<float> grip, bool enabled, bool focused, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawSlider (g, track, grip, enabled, focused);
        return;
    }
    static const auto filledTrack = sliderTrack ({ 1018, 813, 33, 18 }, false);
    static const auto emptyTrack = sliderTrack ({ 1279, 813, 33, 18 }, true);
    static const auto metalGrip = mockup().getClippedImage ({ 1164, 806, 24, 33 });
    const float radius = (float) track.getHeight() * 0.5f;
    const auto gripRadius = grip.getWidth() * 0.23f;

    const Graphics::ScopedSaveState sliderState (g);
    g.setOpacity (1.0f);
    {
        const Graphics::ScopedSaveState trackState (g);
        Path rail;
        rail.addRoundedRectangle (track.toFloat(), radius);
        g.reduceClipRegion (rail);
        sliced (g, emptyTrack, emptyTrack.getBounds(), track, { 0, 11, 0, 11 }, { 0, 6, 0, 6 });
        {
            const Graphics::ScopedSaveState fillState (g);
            const int limit = jlimit (track.getX(), track.getRight(), roundToInt (grip.getCentreX()));
            g.reduceClipRegion (track.withRight (limit));
            if (skin == EditorSkin::hammerDrill)
                sliced (g, filledTrack, filledTrack.getBounds(), track, { 0, 11, 0, 11 }, { 0, 6, 0, 6 });
            else
            {
                const auto area = track.toFloat().reduced (1.0f);
                const auto accent = accentColour (skin);
                g.setGradientFill (ColourGradient (accent, area.getTopLeft(), accent.darker (0.28f), area.getBottomLeft(), false));
                g.fillRoundedRectangle (area, radius - 1.0f);
            }
        }
        if (! enabled) g.fillAll (Colours::black.withAlpha (0.35f));
    }

    if (focused && enabled)
    {
        g.setColour (skin == EditorSkin::hammerDrill ? Colour (0xffffd96a) : accentColour (skin));
        g.drawRoundedRectangle (track.toFloat().reduced (0.5f), radius, 1.0f);
    }

    g.setColour (Colours::black.withAlpha (0.50f));
    g.fillRoundedRectangle (grip.expanded (0.6f).translated (0.0f, 1.0f), gripRadius);
    Path cap;
    cap.addRoundedRectangle (grip, gripRadius);
    g.reduceClipRegion (cap);
    g.setOpacity (1.0f);
    g.drawImage (metalGrip, grip);
    if (! enabled) g.fillAll (Colours::black.withAlpha (0.35f));
}

Rectangle<int> keyArea (Rectangle<int> bounds, int bodyBottom)
{
    // The old keys sit entirely inside this rectangle in the photographed footer.
    const int horizontalInset = 74 * footerCornerWidth (bounds) / footerSourceCorner;
    const auto targetY = footerTargetY (bounds, bodyBottom);
    const int top = (int) std::floor (jmap (916.0, (double) footerSourceY[0], (double) footerSourceY[1],
                                             (double) targetY[0], (double) targetY[1]));
    const int bottom = (int) std::ceil (jmap (994.0, (double) footerSourceY[1], (double) footerSourceY[2],
                                              (double) targetY[1], (double) targetY[2]));
    return Rectangle<int>::leftTopRightBottom (horizontalInset, top, bounds.getRight() - horizontalInset, bottom);
}

void drawKeyWell (Graphics& g, Rectangle<int> target, EditorSkin skin)
{
    if (skin == EditorSkin::cottonCandy)
    {
        cottonCandy::drawPearl (g, target.toFloat().expanded (8.0f, 6.0f), Colours::white, Colour (0xfff095c2), 13.0f);
        return;
    }
    g.setTiledImageFill (blackPlastic(), target.getX(), target.getY(), 1.0f);
    g.fillRect (target);
}

void drawKnob (Graphics& g, Rectangle<float> dial, float angle, EditorSkin skin)
{
    const auto& knob = knobFor (skin);
    const auto centre = dial.getCentre();
    const auto rotor = dial.reduced (dial.getWidth() * 0.025f);
    g.setColour (Colours::black.withAlpha (0.50f));
    g.fillEllipse (dial.translated (0.0f, dial.getHeight() * 0.014f));
    g.setGradientFill (ColourGradient (Colour (0xff414447), dial.getTopLeft(),
                                       Colour (0xff0a0b0b), dial.getBottomLeft(), false));
    g.fillEllipse (dial);

    // The illustrated marker is at twelve o'clock. Turn the whole cap over the fixed collar.
    const auto transform = RectanglePlacement (RectanglePlacement::centred)
                               .getTransformToFit (knob.rotorBounds, rotor)
                               .rotated (angle, centre.x, centre.y);
    g.setOpacity (1.0f);
    g.drawImageTransformed (knob.sprite, transform);
}
} // namespace hammer::artwork
