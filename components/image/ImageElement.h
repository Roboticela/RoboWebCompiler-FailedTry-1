#pragma once

#include "components/rectangle/RectangleElement.h"
#include "rendering/image/ImageAdjust.h"
#include "rendering/image/ImageDecoder.h"

#include <string>
#include <vector>

namespace Runtime {

// How a picture is fitted into the box it was given. The names are the ones the web gives the same
// five answers, because they are the five answers a designer already knows: stretch to fill, fit
// whole inside, fill the box and cut off what hangs over, sit at the size it came at, or do the
// same as fitting whole but never grow past the size it came at.
enum class ImageFit {
    FILL,
    CONTAIN,
    COVER,
    NONE,
    SCALE_DOWN,
};

// An image is a picture drawn in a box. It is built on the rectangle because a picture is a box:
// it carries a box, corners of a radius, a turn, a cursor, a display, and the values every element
// carries, and it is measured, arranged, drawn, and hit the way the rectangle underneath is. What
// it brings is the picture, which arrives as a file the document names.
//
// The picture is not written into the tree the way the art of an SVG is. A file is pixels rather
// than paths, so there is nothing in it a document could pick apart, and the one thing a node can
// say about it is what region of it to draw and how to wash it. That is what `crop` and the look
// are: a region of the file, and the tint, the brightness, the contrast, the saturation, and the
// two mirrors the region is drawn through. Both are applied to the pixels before the draw, because
// the renderer's image cache is keyed by name and two pictures washed two ways have to be two
// images to it, and because a picture that is drawn is drawn once and cached rather than adjusted
// on every pass.
//
// The file is read where the node is first measured, which is where its size is first needed: a
// picture with no size of its own is as big as the picture it holds. A file that cannot be read
// leaves a box named after the file, so a gap in a design is visible where it stands rather than
// silent. A picture with more than one frame is drawn as the frame this moment of the animation
// lands in, so a GIF plays by being drawn.
class ImageElement : public RectangleElement {
public:
    // A picture with no size of its own is the size of the picture it holds, so it does not start
    // as the square a bare rectangle starts as.
    ImageElement();

    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    // The file the picture is read from. It is held so the node still says where its picture came
    // from, which is also what a picture that could not be read is drawn named after.
    void SetSource(std::string value);
    const std::string& Source() const { return source; }
    void SetFit(std::string value);
    ImageFit Fitting() const { return fit; }
    void SetClip(bool value);
    bool Clipped() const { return clip; }
    // The region of the file that is drawn, or nothing when the whole picture is drawn.
    void SetCrop(std::string value);
    ImageCrop Crop() const;
    bool Cropped() const { return cropWidth > 0 && cropHeight > 0; }
    // Where the picture sits in the box when it does not cover it: a fraction of the space left
    // over, so nought is against the far edge, a half is centred, and one is against the near edge.
    // Covering the box is the case where this says which part of the picture is kept instead.
    void SetObjectPosition(std::string value);
    float PositionX() const { return positionX; }
    float PositionY() const { return positionY; }
    void SetTint(Color value);
    bool Tinted() const { return tinted; }
    Color TintColor() const { return tint; }
    void SetBrightness(float value);
    void SetContrast(float value);
    void SetSaturation(float value);
    float Brightness() const { return brightness; }
    float Contrast() const { return contrast; }
    float Saturation() const { return saturation; }
    void SetFlipX(bool value);
    void SetFlipY(bool value);
    bool FlippedX() const { return flipX; }
    bool FlippedY() const { return flipY; }

    // The size the picture itself is drawn at: the region it was cropped to when it was cropped,
    // and the size the file holds when it was not. A picture that could not be read is the box a
    // picture with no file is drawn as. The first frame is the one asked about, because the size
    // of a node is not something an animation is allowed to move.
    Size Intrinsic();
    // The box the picture is drawn in inside the box the node was given, which is where the fitting
    // and the position land. It is the whole box for a picture stretched to fill it.
    Rect PictureBox();

    // The pixels the picture is drawn from at this moment: the frame of the animation, cropped and
    // washed. The name is what tells the renderer's cache one picture from another.
    struct Picture {
        const unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        std::string name;
    };
    bool ReadPicture(Picture& picture);

protected:
    void OnPaint(Renderer& renderer) override;

private:
    bool EnsureDecoded();
    // The radius of this box's corners when all four are the same, and nought when they are not: a
    // picture can carry the corners of its box only when the box has one corner to carry.
    float EvenCornerRadius() const;
    // The two radii of the oval a picture carries when it carries the corners of its box: the corner
    // radius once across and once down, measured in the pixels of the picture. False when the picture
    // carries nothing, which is when it does not fill its box exactly, or when it is drawn larger
    // than it came, because a corner magnified is a corner the picture never held: the arcs of the
    // box would be finer than one pixel of it.
    bool CarriedCorners(float& across, float& down);
    // The frame of the animation this moment of the clock lands in. A still picture is frame zero.
    int FrameIndex(unsigned long long milliseconds) const;
    ImageLook Look() const;

    std::string source;
    ImageFit fit = ImageFit::FILL;
    bool clip = false;
    bool decodedReady = false;
    DecodedImage decoded;
    int cropX = 0;
    int cropY = 0;
    int cropWidth = 0;
    int cropHeight = 0;
    float positionX = 0.5f;
    float positionY = 0.5f;
    bool tinted = false;
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool flipX = false;
    bool flipY = false;
    // The pixels last handed to the renderer, held so a picture that was adjusted once is not
    // adjusted again on every pass. The name beside them says which picture they are, so a frame
    // that changed, or a look that changed, is adjusted afresh.
    std::vector<unsigned char> adjusted;
    int adjustedWidth = 0;
    int adjustedHeight = 0;
    std::string adjustedName;
};

}  // namespace Runtime
