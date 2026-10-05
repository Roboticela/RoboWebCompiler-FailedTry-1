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

int ReadSvgNumber(const std::string& text, const char* key, int fallback) {
    const std::string token = std::string(key) + "=\"";
    const std::size_t found = text.find(token);
    if (found == std::string::npos) {
        return fallback;
    }
    return std::atoi(text.c_str() + found + token.size());
}

unsigned Channel(const std::string& fill, int index, unsigned fallback) {
    if (fill.size() < 7 || fill[0] != '#') {
        return fallback;
    }
    const auto hex = [](char character) {
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + 10;
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + 10;
        }
        return 0;
    };
    const int start = 1 + index * 2;
    return static_cast<unsigned>(hex(fill[start]) * 16 + hex(fill[start + 1]));
}

bool RasterSvg(const unsigned char* data, int size, DecodedImage& image) {
    const std::string text(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
    int width = ReadSvgNumber(text, "width", 0);
    int height = ReadSvgNumber(text, "height", 0);
    if (width < 1 || height < 1 || width > 256 || height > 256) {
        return false;
    }
    const std::size_t fillAt = text.find("fill=\"");
    std::string fill = "#000000";
    if (fillAt != std::string::npos) {
        const std::size_t end = text.find('"', fillAt + 6);
        if (end != std::string::npos) {
            fill = text.substr(fillAt + 6, end - (fillAt + 6));
        }
    }
    const unsigned red = Channel(fill, 0, 0);
    const unsigned green = Channel(fill, 1, 0);
    const unsigned blue = Channel(fill, 2, 0);
    ImageFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra.assign(static_cast<std::size_t>(width * height * 4), 0);
    for (int pixel = 0; pixel < width * height; ++pixel) {
        frame.bgra[static_cast<std::size_t>(pixel * 4)] = static_cast<unsigned char>(blue);
        frame.bgra[static_cast<std::size_t>(pixel * 4 + 1)] = static_cast<unsigned char>(green);
        frame.bgra[static_cast<std::size_t>(pixel * 4 + 2)] = static_cast<unsigned char>(red);
        frame.bgra[static_cast<std::size_t>(pixel * 4 + 3)] = 255;
    }
    image.format = "svg";
    image.frames.push_back(std::move(frame));
    return true;
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
        return RasterSvg(data, size, image);
    }
    return DecodeRaster(data, size, image);
}

}  // namespace Runtime
