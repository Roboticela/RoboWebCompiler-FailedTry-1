#include "rendering/image/ImageDecoder.h"

#include "rendering/image/ImageCodec.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Runtime {
namespace {

bool LooksLikeSvg(const unsigned char* data, int size) {
    int index = 0;
    while (index < size && (data[index] == ' ' || data[index] == '\n' || data[index] == '\r' || data[index] == '\t')) {
        ++index;
    }
    const char* text = reinterpret_cast<const char*>(data + index);
    const int remaining = size - index;
    return (remaining >= 4 && std::strncmp(text, "<svg", 4) == 0) || (remaining >= 5 && std::strncmp(text, "<?xml", 5) == 0);
}

bool DecodeRaster(const unsigned char* data, int size, DecodedImage& image) {
    if (size >= 8 && std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return DecodePngImage(data, size, image);
    }
    if (size >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return DecodeJpegImage(data, size, image);
    }
    if (size >= 6 && (std::memcmp(data, "GIF87a", 6) == 0 || std::memcmp(data, "GIF89a", 6) == 0)) {
        return DecodeGifImage(data, size, image);
    }
    if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WEBP", 4) == 0) {
        return DecodeWebpImage(data, size, image);
    }
    if (size >= 12 && std::memcmp(data + 4, "ftyp", 4) == 0) {
        return DecodeAvifImage(data, size, image);
    }
    return false;
}

}  // namespace

bool DecodeImageFile(const std::string& path, DecodedImage& image) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 8) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) {
        return false;
    }
    return DecodeImageBytes(bytes.data(), static_cast<int>(bytes.size()), image);
}

bool DecodeImageBytes(const unsigned char* data, int size, DecodedImage& image) {
    image = {};
    if (data == nullptr || size < 8) {
        return false;
    }
    if (LooksLikeSvg(data, size)) {
        return DecodeSvgBytes(data, size, image);
    }
    return DecodeRaster(data, size, image);
}

}  // namespace Runtime
