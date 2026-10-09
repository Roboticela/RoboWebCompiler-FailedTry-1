#include "rendering/image/ImageDecoder.h"

#include "rendering/image/ImageCodec.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Runtime {
namespace {

// The decoder a host registered, or nothing when it registered none. It is one value rather than a
// list because it stands for what the machine can read, and a machine has one answer to that.
ImageFormatProvider installation = nullptr;

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
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        return DecodeBmpImage(data, size, image);
    }
    // An icon and a cursor are the same kind of file, one carrying pictures and the other carrying
    // pictures with a hotspot in them, which is the only thing about a cursor the runtime ignores.
    if (size >= 4 && data[0] == 0 && data[1] == 0 && (data[2] == 1 || data[2] == 2) && data[3] == 0) {
        return DecodeIcoImage(data, size, image);
    }
    // A tagged picture names the order of its own bytes in the first two of them, and then the
    // number that says it is one of these, which is the same figure read either way round.
    if (size >= 4 && ((data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0) ||
                      (data[0] == 'M' && data[1] == 'M' && data[2] == 0 && data[3] == 42))) {
        return DecodeTiffImage(data, size, image);
    }
    if (size >= 12 && std::memcmp(data + 4, "ftyp", 4) == 0) {
        return DecodeAvifImage(data, size, image);
    }
    return false;
}

}  // namespace

void SetImageFormatProvider(ImageFormatProvider provider) {
    installation = provider;
}

ImageFormatProvider GetImageFormatProvider() {
    return installation;
}

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
    if (DecodeRaster(data, size, image)) {
        return true;
    }
    // Every reader written here has passed on the file, and a decoder the host registered gets the
    // last word on it. The bytes are offered to that decoder along with the format the container
    // named, and it answers with the pixels or with nothing. No decoder is registered by a program
    // that never asked for one, so a file the core cannot read behaves the same wherever it is
    // opened rather than as a fact about the machine underneath.
    if (ImageFormatProvider provider = installation) {
        DecodedImage read;
        if (provider(data, size, image.format, read) && !read.frames.empty()) {
            if (!read.format.empty()) {
                image.format = read.format;
            }
            if (read.declaredWidth > 0 && read.declaredHeight > 0) {
                image.declaredWidth = read.declaredWidth;
                image.declaredHeight = read.declaredHeight;
            }
            image.frames = std::move(read.frames);
            return true;
        }
    }
    return false;
}

}  // namespace Runtime
