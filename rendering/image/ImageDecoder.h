#pragma once

#include <string>
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

bool DecodeImageBytes(const unsigned char* data, int size, DecodedImage& image);
bool DecodeImageFile(const std::string& path, DecodedImage& image);

}  // namespace Runtime
