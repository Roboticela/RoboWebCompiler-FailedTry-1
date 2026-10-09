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
// buffer that comes back is premultiplied, which is the form the platform composites: a channel is
// handed over with its alpha already folded into it. `premultiplied` says whether the pixels that
// came in are in that form already; a buffer that is not is unfolded before the look is applied and
// folded back in afterwards, so a look is exact on a pixel that is not fully opaque rather than
// merely close. The region is clamped to the frame, so a crop written past the edge of a picture is
// the part of it that exists rather than nothing at all. The output is always written, even when
// nothing changes, so a caller holds one buffer: it is the frame itself, made ready to draw.
void AdjustImage(const unsigned char* bgra, int width, int height, bool premultiplied, const ImageCrop& crop,
                 const ImageLook& look, std::vector<unsigned char>& out, int& outWidth, int& outHeight);

// A name for the crop and the look a picture is drawn with, so a picture drawn two ways is two
// pictures to the renderer's cache rather than one. The name is empty when neither was asked for,
// which is what lets a plain picture keep the key it always had.
std::string ImageLookName(const ImageCrop& crop, const ImageLook& look);

// A picture drawn in a box with rounded corners is the shape of the box: the corners are the arcs
// the box cuts, so they are written into the picture itself rather than cut out of it by the
// platform on every frame. A cut is a mask the platform has to composite, and a picture drawn every
// frame would otherwise be a mask composited every frame, while a corner written into the picture
// is written once, when the picture is made ready to draw.
//
// The arcs are given in the pixels of the picture rather than of the box, and once across and once
// down, because the platform draws the picture at whatever size the box asks for: a round corner of
// the box is an oval of the picture whenever the box and the picture are not the same shape, and it
// is the outside of that oval that is cleared here. The pixels are premultiplied, so a cleared
// corner is scaled in all four of its channels rather than emptied of its alpha alone.
void RoundImageCorners(std::vector<unsigned char>& bgra, int width, int height, float acrossRadius, float downRadius);

}  // namespace Runtime
