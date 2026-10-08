#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

struct ImageFrame {
    int width = 0;
    int height = 0;
    int delayMs = 0;
    // Whether the channels of `bgra` already have their alpha folded into them. Both forms are read
    // by the runtime — a decoder that lays a picture down by compositing one shape over another
    // accumulates premultiplied pixels, while one that writes the channels a file held writes them
    // straight — so the frame says which it is, and the buffer a picture is finally drawn from is
    // made the form the platform composites either way.
    bool premultiplied = false;
    std::vector<unsigned char> bgra;
};

struct DecodedImage {
    std::string format;
    std::vector<ImageFrame> frames;
};

struct SvgDecodeOptions {
    int maxEdge = 0;
    unsigned char currentRed = 0;
    unsigned char currentGreen = 0;
    unsigned char currentBlue = 0;
};

bool DecodeImageBytes(const unsigned char* data, int size, DecodedImage& image);
bool DecodeImageFile(const std::string& path, DecodedImage& image);
bool DecodeSvgBytes(const unsigned char* data, int size, DecodedImage& image, const SvgDecodeOptions& options = {});
std::string VectorXmlFromSvg(std::string_view svg, std::string_view name, unsigned char red, unsigned char green, unsigned char blue);

}  // namespace Runtime
