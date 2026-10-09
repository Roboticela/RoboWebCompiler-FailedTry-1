#include "components/image/ImageElement.h"

#include "compiler/types/ValueParser.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace Runtime {
namespace {

// A picture that could not be read still covers a box, so the gap in a design is visible where it
// stands. The box is the one every design tool draws for a picture it does not have yet.
constexpr float IMAGE_UNKNOWN_WIDTH = 120.0f;
constexpr float IMAGE_UNKNOWN_HEIGHT = 80.0f;
const Color IMAGE_UNKNOWN_FILL{0.91f, 0.92f, 0.93f, 1.0f};
const Color IMAGE_UNKNOWN_INK{0.094f, 0.094f, 0.102f, 1.0f};
constexpr float TURN_EPSILON = 0.01f;

// How long the clock has been running, in milliseconds. It is the standard library's steady clock
// rather than the count of milliseconds the operating system hands out, because a picture with more
// than one frame is a fact about the picture and not about the machine: the element reads the time
// it has been shown for and nothing else about where it is running.
unsigned long long ElapsedMilliseconds() {
    using SteadyClock = std::chrono::steady_clock;
    static const SteadyClock::time_point started = SteadyClock::now();
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - started).count());
}

// The same folding every other reading of a keyword uses: the case does not matter, and neither do
// the marks a document might write a name with.
std::string FoldToken(const std::string& text) {
    std::string folded;
    for (unsigned char character : text) {
        if (character == '_' || character == '-' || character == ' ') {
            continue;
        }
        folded.push_back(static_cast<char>(std::tolower(character)));
    }
    return folded;
}

// The five answers a document can give about fitting a picture, folded onto the five answers the
// node draws. A picture whose fit was written as nothing a reader knows is fitted whole, which is
// the answer that never draws past the box it was given.
ImageFit ParseFit(const std::string& value) {
    const std::string folded = FoldToken(value);
    if (folded == "fill" || folded == "stretch") {
        return ImageFit::FILL;
    }
    if (folded == "cover") {
        return ImageFit::COVER;
    }
    if (folded == "none") {
        return ImageFit::NONE;
    }
    if (folded == "scaledown") {
        return ImageFit::SCALE_DOWN;
    }
    return ImageFit::CONTAIN;
}

// The words a position is written with, split the way a document writes a list of numbers.
std::vector<std::string> SplitWords(const std::string& value) {
    std::vector<std::string> words;
    std::string current;
    for (char character : value) {
        if (character == ' ' || character == ',' || character == '\t') {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(character);
    }
    if (!current.empty()) {
        words.push_back(current);
    }
    return words;
}

bool Truthy(const std::string& value) {
    const std::string folded = FoldToken(value);
    return folded != "false" && folded != "0" && folded != "none" && folded != "hidden" && folded != "no";
}

// The width a box's edge is allowed to be out by and still be the same edge: two boxes worked out
// from the same lengths land on each other exactly, so this only has to hold a number that came
// through a division.
constexpr float EDGE_EPSILON = 0.01f;

// Two boxes that lie on each other: the same corner and the same two sides.
bool SameBox(const Rect& one, const Rect& other) {
    return std::fabs(one.x - other.x) <= EDGE_EPSILON && std::fabs(one.y - other.y) <= EDGE_EPSILON &&
           std::fabs(one.width - other.width) <= EDGE_EPSILON && std::fabs(one.height - other.height) <= EDGE_EPSILON;
}

// The rectangle a rounded box leaves whole: the box brought in by its widest corner. A picture that
// stands inside it never reaches a corner, so the corners of the box cannot cut it and it needs no
// mask at all.
bool InsideCorners(const Rect& area, float corner, const Rect& box) {
    return box.x >= area.x + corner - EDGE_EPSILON && box.y >= area.y + corner - EDGE_EPSILON &&
           box.x + box.width <= area.x + area.width - corner + EDGE_EPSILON &&
           box.y + box.height <= area.y + area.height - corner + EDGE_EPSILON;
}

}  // namespace

ImageElement::ImageElement() {
    // A picture with no size of its own is the size of the picture it holds, so it does not start
    // as the square a bare rectangle starts as.
    SetWidth(Length::Fit());
    SetHeight(Length::Fit());
}

NodeKind ImageElement::Kind() const {
    return NodeKind::IMAGE;
}

void ImageElement::SetSource(std::string value) {
    source = std::move(value);
    // The picture is read again because the file it is read from changed, which is also what makes
    // the pixels the renderer was last handed no longer the pixels this node draws.
    decodedReady = false;
    decoded = {};
    adjusted.clear();
    adjustedWidth = 0;
    adjustedHeight = 0;
    adjustedName.clear();
}

void ImageElement::SetFit(std::string value) {
    fit = ParseFit(value);
}

void ImageElement::SetClip(bool value) {
    clip = value;
}

void ImageElement::SetCrop(std::string value) {
    // A region is four numbers in one attribute, the way a view box is: its place in the picture and
    // then its size. A region with no size is not a region, so it is the whole picture.
    const char* text = value.c_str();
    char* end = nullptr;
    const float left = std::strtof(text, &end);
    if (end == text) {
        cropX = cropY = cropWidth = cropHeight = 0;
        return;
    }
    const char* next = end;
    const float top = std::strtof(next, &end);
    if (end == next) {
        cropX = cropY = cropWidth = cropHeight = 0;
        return;
    }
    next = end;
    const float regionWidth = std::strtof(next, &end);
    if (end == next) {
        cropX = cropY = cropWidth = cropHeight = 0;
        return;
    }
    next = end;
    const float regionHeight = std::strtof(next, &end);
    if (end == next || regionWidth < 1.0f || regionHeight < 1.0f) {
        cropX = cropY = cropWidth = cropHeight = 0;
        return;
    }
    cropX = static_cast<int>(std::lround(left));
    cropY = static_cast<int>(std::lround(top));
    cropWidth = static_cast<int>(std::lround(regionWidth));
    cropHeight = static_cast<int>(std::lround(regionHeight));
}

ImageCrop ImageElement::Crop() const {
    ImageCrop region;
    if (!Cropped()) {
        return region;
    }
    region.x = cropX;
    region.y = cropY;
    region.width = cropWidth;
    region.height = cropHeight;
    return region;
}

void ImageElement::SetObjectPosition(std::string value) {
    // Where a picture sits is written the way the web writes it: a keyword, a fraction, or a
    // percentage, once or twice. One word goes across, because that is the answer a document that
    // says only `left` is giving, and a word that names an edge up or down goes up or down.
    const std::vector<std::string> words = SplitWords(value);
    bool haveHorizontal = false;
    bool haveVertical = false;
    float horizontal = 0.5f;
    float vertical = 0.5f;
    for (const std::string& word : words) {
        const std::string folded = FoldToken(word);
        if (folded == "left") {
            horizontal = 0.0f;
            haveHorizontal = true;
            continue;
        }
        if (folded == "right") {
            horizontal = 1.0f;
            haveHorizontal = true;
            continue;
        }
        if (folded == "top") {
            vertical = 0.0f;
            haveVertical = true;
            continue;
        }
        if (folded == "bottom") {
            vertical = 1.0f;
            haveVertical = true;
            continue;
        }
        if (folded == "center" || folded == "centre" || folded == "middle") {
            if (!haveHorizontal && !haveVertical) {
                haveHorizontal = true;
                haveVertical = true;
            } else if (!haveHorizontal) {
                haveHorizontal = true;
            } else {
                haveVertical = true;
            }
            continue;
        }
        const char* text = word.c_str();
        char* end = nullptr;
        const float number = std::strtof(text, &end);
        if (end == text) {
            continue;
        }
        const bool percent = end != nullptr && *end == '%';
        const float fraction = std::clamp(percent ? number / 100.0f : number, 0.0f, 1.0f);
        if (!haveHorizontal) {
            horizontal = fraction;
            haveHorizontal = true;
        } else {
            vertical = fraction;
            haveVertical = true;
        }
    }
    positionX = horizontal;
    positionY = vertical;
}

void ImageElement::SetTint(Color value) {
    tint = value;
    tinted = true;
}

void ImageElement::SetBrightness(float value) {
    brightness = value < 0.0f ? 0.0f : value;
}

void ImageElement::SetContrast(float value) {
    contrast = value < 0.0f ? 0.0f : value;
}

void ImageElement::SetSaturation(float value) {
    saturation = value < 0.0f ? 0.0f : value;
}

void ImageElement::SetFlipX(bool value) {
    flipX = value;
}

void ImageElement::SetFlipY(bool value) {
    flipY = value;
}

bool ImageElement::EnsureDecoded() {
    // The file is read once, where the size of the node or the pixels it draws are first needed.
    // A reader that failed leaves the picture empty, which is drawn as the box a picture with no
    // file is drawn as rather than as nothing at all.
    if (!decodedReady && !source.empty()) {
        decodedReady = true;
        DecodeImageFile(source, decoded);
    }
    return !decoded.frames.empty();
}

int ImageElement::FrameIndex(unsigned long long milliseconds) const {
    const std::size_t count = decoded.frames.size();
    if (count <= 1) {
        return 0;
    }
    unsigned long long total = 0;
    for (const ImageFrame& frame : decoded.frames) {
        total += static_cast<unsigned long long>(frame.delayMs > 0 ? frame.delayMs : 100);
    }
    if (total == 0) {
        return 0;
    }
    // A frame with no delay written is held for a tenth of a second, which is what a reader that
    // finds no timing still has to draw something for.
    const unsigned long long at = milliseconds % total;
    unsigned long long spent = 0;
    for (std::size_t index = 0; index < count; ++index) {
        spent += static_cast<unsigned long long>(decoded.frames[index].delayMs > 0 ? decoded.frames[index].delayMs : 100);
        if (at < spent) {
            return static_cast<int>(index);
        }
    }
    return static_cast<int>(count - 1);
}

ImageLook ImageElement::Look() const {
    ImageLook look;
    look.tinted = tinted;
    look.tint = tint;
    look.brightness = brightness;
    look.contrast = contrast;
    look.saturation = saturation;
    look.flipX = flipX;
    look.flipY = flipY;
    return look;
}

Size ImageElement::Intrinsic() {
    if (Cropped()) {
        // A cropped picture is drawn at the size of the region, which is the part of the file the
        // document asked to see rather than the whole of it.
        return Size{static_cast<float>(cropWidth), static_cast<float>(cropHeight)};
    }
    if (EnsureDecoded() && !decoded.frames.empty()) {
        const ImageFrame& first = decoded.frames.front();
        if (first.width > 0 && first.height > 0) {
            return Size{static_cast<float>(first.width), static_cast<float>(first.height)};
        }
    }
    // The pixels could not be produced, but the file still says how big the picture in it is: an
    // AVIF whose codec the runtime does not carry lays out at the size its container states, so the
    // gap it leaves is the gap the picture would have covered rather than a box of a fixed size.
    if (decoded.declaredWidth > 0 && decoded.declaredHeight > 0) {
        return Size{static_cast<float>(decoded.declaredWidth), static_cast<float>(decoded.declaredHeight)};
    }
    return Size{IMAGE_UNKNOWN_WIDTH, IMAGE_UNKNOWN_HEIGHT};
}

Rect ImageElement::PictureBox() {
    const Rect area = Bounds();
    if (fit == ImageFit::FILL) {
        // Stretching is the one fitting that is the whole box, so nothing has to be worked out.
        return area;
    }
    const Size art = Intrinsic();
    if (art.width < 1.0f || art.height < 1.0f) {
        return area;
    }
    float scale = 1.0f;
    if (fit == ImageFit::COVER) {
        scale = std::max(area.width / art.width, area.height / art.height);
    } else if (fit == ImageFit::CONTAIN || fit == ImageFit::SCALE_DOWN) {
        scale = std::min(area.width / art.width, area.height / art.height);
        if (fit == ImageFit::SCALE_DOWN) {
            // A picture that already fits is not blown up, which is the one thing that makes this
            // different from fitting whole.
            scale = std::min(1.0f, scale);
        }
    }
    Rect box{0.0f, 0.0f, art.width * scale, art.height * scale};
    // The space the picture did not cover is shared out by the position: nought against one edge,
    // one against the other, a half split evenly. A picture that covers the box has no space left
    // over, so this is also which part of the picture is kept.
    box.x = area.x + (area.width - box.width) * positionX;
    box.y = area.y + (area.height - box.height) * positionY;
    return box;
}

bool ImageElement::ReadPicture(Picture& picture) {
    if (!EnsureDecoded() || decoded.frames.empty()) {
        return false;
    }
    // Which frame is drawn is a fact about the clock, so it is read once and both the pixels and
    // the name they are cached under are the same frame.
    const int index = FrameIndex(ElapsedMilliseconds());
    const ImageFrame& frame = decoded.frames[static_cast<std::size_t>(index)];
    if (frame.width < 1 || frame.height < 1 || frame.bgra.empty()) {
        return false;
    }
    const ImageCrop region = Crop();
    const ImageLook look = Look();
    // The name the renderer caches under says which file, which frame of it, and which crop and
    // look were asked for, so two pictures that are pixels apart are two images to the cache rather
    // than one drawn twice as the first of them.
    std::string name = source + "#" + std::to_string(index) + "#" + ImageLookName(region, look);
    // A picture that fills its box exactly is drawn as the rounded box itself, so the corners of the
    // box are folded into the picture once, when it is made ready to draw, rather than cut out of it
    // by the platform on every frame, which is a mask the platform composites afresh for each picture
    // of each frame. The radii are part of the name the pixels are held under, because the same file
    // drawn into two boxes of different shapes is two pictures with two different corners in it.
    float across = 0.0f;
    float down = 0.0f;
    const bool carried = CarriedCorners(across, down);
    if (carried) {
        char rounded[64];
        std::snprintf(rounded, sizeof(rounded), ":r%.3f:%.3f", static_cast<double>(across), static_cast<double>(down));
        name += rounded;
    }
    if (name != adjustedName) {
        AdjustImage(frame.bgra.data(), frame.width, frame.height, frame.premultiplied, region, look, adjusted, adjustedWidth,
                    adjustedHeight);
        if (carried) {
            RoundImageCorners(adjusted, adjustedWidth, adjustedHeight, across, down);
        }
        adjustedName = name;
    }
    if (adjusted.empty() || adjustedWidth < 1 || adjustedHeight < 1) {
        return false;
    }
    picture.pixels = adjusted.data();
    picture.width = adjustedWidth;
    picture.height = adjustedHeight;
    picture.name = adjustedName;
    return true;
}

float ImageElement::EvenCornerRadius() const {
    const float corner = CornerRadius(0);
    if (corner <= TURN_EPSILON) {
        return 0.0f;
    }
    for (int index = 1; index < 4; ++index) {
        if (std::fabs(CornerRadius(index) - corner) > TURN_EPSILON) {
            return 0.0f;
        }
    }
    return corner;
}

bool ImageElement::CarriedCorners(float& across, float& down) {
    across = 0.0f;
    down = 0.0f;

    const float corner = EvenCornerRadius();
    if (corner <= 0.0f) {
        return false;
    }
    const Rect box = PictureBox();
    const Rect area = Bounds();
    if (box.width <= 0.0f || box.height <= 0.0f || !SameBox(box, area)) {
        return false;
    }
    // A round corner of the box is an oval of the picture whenever the box and the picture are not
    // the same shape, so the picture is told the two radii of the oval it carries. A picture drawn
    // larger than it came has no pixel small enough to hold those arcs, so it is left to the
    // platform: a corner cut out of a magnified picture would be a corner the file never had.
    const Size art = Intrinsic();
    if (art.width < 1.0f || art.height < 1.0f || box.width > art.width + EDGE_EPSILON || box.height > art.height + EDGE_EPSILON) {
        return false;
    }
    across = corner * art.width / box.width;
    down = corner * art.height / box.height;
    return across > 0.0f && down > 0.0f;
}

Size ImageElement::Measure(TextMeasurer&, float availableWidth, float availableHeight, bool) {
    const Size art = Intrinsic();
    float resolvedWidth = art.width;
    float resolvedHeight = art.height;
    // A picture has nothing inside it to hug, so a fixed, filled, or percentage length is the only
    // thing that moves it away from the size of the picture itself.
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL) {
        resolvedWidth = Width().Resolve(availableWidth, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL) {
        resolvedHeight = Height().Resolve(availableHeight, resolvedHeight);
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    return FinishMeasure(availableWidth, availableHeight);
}

void ImageElement::ApplyBound(const std::string& property, const std::string& value) {
    // A box is bound the way the rectangle underneath is, and the only values left to answer for
    // are the ones about the picture itself.
    RectangleElement::ApplyBound(property, value);
    if (property == "source") {
        SetSource(value);
    } else if (property == "fit") {
        SetFit(value);
    } else if (property == "clip") {
        SetClip(Truthy(value));
    } else if (property == "crop") {
        SetCrop(value);
    } else if (property == "objectPosition") {
        SetObjectPosition(value);
    } else if (property == "tint") {
        if (const std::optional<Color> color = ParseColor(value)) {
            SetTint(*color);
        }
    } else if (property == "brightness") {
        SetBrightness(std::strtof(value.c_str(), nullptr));
    } else if (property == "contrast") {
        SetContrast(std::strtof(value.c_str(), nullptr));
    } else if (property == "saturation") {
        SetSaturation(std::strtof(value.c_str(), nullptr));
    } else if (property == "flipX") {
        SetFlipX(Truthy(value));
    } else if (property == "flipY") {
        SetFlipY(Truthy(value));
    }
}

void ImageElement::OnPaint(Renderer& renderer) {
    const Rect area = Bounds();
    if (area.width <= 0.0f || area.height <= 0.0f) {
        return;
    }
    Picture picture;
    if (ReadPicture(picture)) {
        const Rect box = PictureBox();
        const float turn = Rotation();
        const bool turned = std::fabs(turn) > TURN_EPSILON;
        if (turned) {
            renderer.PushRotation(turn, area.x + area.width * 0.5f, area.y + area.height * 0.5f);
        }
        // A picture that does not stand inside its box is cut off at the box, whether it was asked
        // to be or because covering the box is exactly the case where it hangs over. A box with
        // corners is cut at those corners as well, whether the picture hangs over it or not, because
        // the box the picture sits in is the rounded box this node is drawn as rather than the
        // rectangle around it.
        const bool held = clip || fit == ImageFit::COVER;
        const bool rounded = Radius() > TURN_EPSILON;
        // The picture carries the corners of the box itself when it fills the box and the box has one
        // corner to carry, and a picture that stands inside the widest corner of the box is never
        // reached by the corners at all. Neither case asks the platform for a mask.
        float across = 0.0f;
        float down = 0.0f;
        const bool carried = CarriedCorners(across, down);
        const bool clear = rounded && InsideCorners(area, Radius(), box);
        bool cut = false;
        if (rounded && !carried && !clear) {
            // The box's own outline, which is the shape the corners of this node already draw, so
            // the picture is cut off at exactly the box it is drawn in.
            const std::string outline = CornerPath(area.width, area.height);
            renderer.PushClipPath(area, outline);
            cut = true;
        } else if (!rounded && held) {
            renderer.PushClip(area);
            cut = true;
        }
        renderer.DrawCachedImage(picture.name, box, picture.pixels, picture.width, picture.height);
        if (cut) {
            renderer.PopClip();
        }
        if (turned) {
            renderer.PopTransform();
        }
        return;
    }
    // A picture that could not be read is drawn as the box it would have covered, named after the
    // file the document asked for, so a design shows the gap rather than showing nothing.
    renderer.FillRectangle(area, IMAGE_UNKNOWN_FILL, Radius());
    renderer.StrokeRectangle(area, StrokeColor(), 1.0f, Radius());
    TextStyle style;
    style.fontSize = 12.0f;
    style.color = IMAGE_UNKNOWN_INK;
    style.horizontal = TextAlign::CENTER;
    style.vertical = TextAlign::CENTER;
    renderer.DrawString(source.empty() ? "Image" : source, area, style);
}

}  // namespace Runtime
