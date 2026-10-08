#pragma once

#include "engine/math/Color.h"

#include <string>
#include <vector>

namespace Runtime {

// The part of a picture that is drawn, written in the pixels of the picture itself rather than in
// the box it is drawn in, because that is the only place the region is a fact about the file. A
// crop that was not written holds no region, which is the whole picture.
struct ImageCrop {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool Used() const { return width > 0 && height > 0; }
};

// How a picture is drawn: the colour it is washed with, how bright, contrasty and saturated it is,
// and the two mirrors. Every value that changes nothing is the default, so a picture a document
// never spoke about is drawn as the file held it.
struct ImageLook {
    bool tinted = false;
    Color tint{1.0f, 1.0f, 1.0f, 1.0f};
    float brightness = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool flipX = false;
    bool flipY = false;

    // True when the look is the picture itself, which is the case that needs no work at all.
    bool Neutral() const {
        return !tinted && brightness == 1.0f && contrast == 1.0f && saturation == 1.0f && !flipX && !flipY;
    }
};

// The pixels a frame is drawn from, after the region it was cropped to and the look it was given.
// The pixels are BGRA, which is the order every image in the runtime is handed over in, and the
// channels are used as the decoder wrote them: a look multiplies them rather than dividing by the
// alpha beside them, so a picture that is opaque, which is nearly every picture, is exact. The
// region is clamped to the frame, so a crop written past the edge of a picture is the part of it
// that exists rather than nothing at all. The output is always written, even when nothing changes,
// so a caller holds one buffer: it is the frame itself, copied.
void AdjustImage(const unsigned char* bgra, int width, int height, const ImageCrop& crop, const ImageLook& look,
                 std::vector<unsigned char>& out, int& outWidth, int& outHeight);

// A name for the crop and the look a picture is drawn with, so a picture drawn two ways is two
// pictures to the renderer's cache rather than one. The name is empty when neither was asked for,
// which is what lets a plain picture keep the key it always had.
std::string ImageLookName(const ImageCrop& crop, const ImageLook& look);

}  // namespace Runtime
