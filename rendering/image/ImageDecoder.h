#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

struct ImageFrame {
    int width = 0;
    int height = 0;
    int delayMs = 0;
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
