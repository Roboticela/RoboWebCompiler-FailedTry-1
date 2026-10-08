#include "rendering/image/ImageAdjust.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Runtime {
namespace {

// A channel the platform is handed, with its alpha folded back in. The platform composites
// premultiplied pixels, so a colour that has been decided is given over as the part of it that is
// there, which is also what keeps a channel no brighter than the alpha beside it.
unsigned char Folded(float straight, float alpha) {
    return static_cast<unsigned char>(std::lround(std::clamp(straight, 0.0f, 255.0f) * (alpha / 255.0f)));
}

// The brightness of a colour to a human eye, which is what desaturating and resaturating is
// measured against. The weights are the ones the sRGB primaries are seen with.
float Luma(float red, float green, float blue) {
    return 0.2126f * red + 0.7152f * green + 0.0722f * blue;
}

}  // namespace

void AdjustImage(const unsigned char* bgra, int width, int height, bool premultiplied, const ImageCrop& crop,
                 const ImageLook& look, std::vector<unsigned char>& out, int& outWidth, int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    out.clear();
    if (bgra == nullptr || width < 1 || height < 1) {
        return;
    }
    int left = 0;
    int top = 0;
    int copyWidth = width;
    int copyHeight = height;
    if (crop.Used()) {
        left = std::clamp(crop.x, 0, width - 1);
        top = std::clamp(crop.y, 0, height - 1);
        copyWidth = std::clamp(crop.width, 1, width - left);
        copyHeight = std::clamp(crop.height, 1, height - top);
    }
    outWidth = copyWidth;
    outHeight = copyHeight;
    out.resize(static_cast<std::size_t>(copyWidth) * static_cast<std::size_t>(copyHeight) * 4u);
    const bool neutral = look.Neutral();
    const float tintRed = look.tinted ? look.tint.r : 1.0f;
    const float tintGreen = look.tinted ? look.tint.g : 1.0f;
    const float tintBlue = look.tinted ? look.tint.b : 1.0f;
    for (int row = 0; row < copyHeight; ++row) {
        const int sourceRow = top + (look.flipY ? copyHeight - 1 - row : row);
        for (int column = 0; column < copyWidth; ++column) {
            const int sourceColumn = left + (look.flipX ? copyWidth - 1 - column : column);
            const unsigned char* from = bgra + (static_cast<std::size_t>(sourceRow) * static_cast<std::size_t>(width) +
                                               static_cast<std::size_t>(sourceColumn)) * 4u;
            unsigned char* to = out.data() + (static_cast<std::size_t>(row) * static_cast<std::size_t>(copyWidth) +
                                             static_cast<std::size_t>(column)) * 4u;
            if (neutral && premultiplied) {
                // The pixels are already the form the platform is handed and the look changes
                // nothing, so the frame is passed on as it was read.
                std::memcpy(to, from, 4);
                continue;
            }
            const float alpha = static_cast<float>(from[3]);
            float blue = static_cast<float>(from[0]);
            float green = static_cast<float>(from[1]);
            float red = static_cast<float>(from[2]);
            if (premultiplied) {
                // A premultiplied channel is a colour with its alpha already in it, so the alpha is
                // taken back out before the look is applied: the look is about the colour, and the
                // colour is not what stands in the buffer while the pixel is translucent.
                if (alpha > 0.0f) {
                    const float unfold = 255.0f / alpha;
                    blue *= unfold;
                    green *= unfold;
                    red *= unfold;
                } else {
                    blue = green = red = 0.0f;
                }
            }
            blue *= tintBlue;
            green *= tintGreen;
            red *= tintRed;
            blue *= look.brightness;
            green *= look.brightness;
            red *= look.brightness;
            // Contrast pulls the channels away from the middle of the range, so a value of one
            // leaves a channel where it was, a value above one spreads them out, and a value below
            // one gathers them together.
            blue = (blue - 127.5f) * look.contrast + 127.5f;
            green = (green - 127.5f) * look.contrast + 127.5f;
            red = (red - 127.5f) * look.contrast + 127.5f;
            const float grey = Luma(red, green, blue);
            blue = grey + (blue - grey) * look.saturation;
            green = grey + (green - grey) * look.saturation;
            red = grey + (red - grey) * look.saturation;
            to[0] = Folded(blue, alpha);
            to[1] = Folded(green, alpha);
            to[2] = Folded(red, alpha);
            to[3] = from[3];
        }
    }
}

std::string ImageLookName(const ImageCrop& crop, const ImageLook& look) {
    if (!crop.Used() && look.Neutral()) {
        return {};
    }
    char numbers[160];
    std::snprintf(numbers, sizeof(numbers), "%.3f:%.3f:%.3f:%.3f:%.3f:%.3f", static_cast<double>(look.brightness),
                  static_cast<double>(look.contrast), static_cast<double>(look.saturation),
                  static_cast<double>(look.tinted ? look.tint.r : 0.0f), static_cast<double>(look.tinted ? look.tint.g : 0.0f),
                  static_cast<double>(look.tinted ? look.tint.b : 0.0f));
    std::string name(numbers);
    if (look.flipX) {
        name += ":x";
    }
    if (look.flipY) {
        name += ":y";
    }
    if (crop.Used()) {
        name += ":c" + std::to_string(crop.x) + "," + std::to_string(crop.y) + "," + std::to_string(crop.width) + "," +
                std::to_string(crop.height);
    }
    return name;
}

}  // namespace Runtime
