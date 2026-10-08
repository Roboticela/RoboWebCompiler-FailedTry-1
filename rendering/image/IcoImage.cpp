#include "rendering/image/ImageCodec.h"

#include <cstring>
#include <vector>

namespace Runtime {
namespace {

// An icon is a file of several pictures of the same thing at different sizes, and the one drawn is
// the largest, because a picture drawn smaller is drawn as well as one drawn larger. Each picture in
// it is one of two things: a whole PNG, which is read by the reader written for PNG, or a bitmap
// without the file header a bitmap has of its own, which is given the header it is missing and read
// by the reader written for bitmaps. Both readers are portable, so nothing about an icon is a fact
// about the machine.
//
// The bitmap form states its height twice over, because the transparency it carries is written after
// the colours as a second picture of one bit a pixel. The colours are read as the picture they are,
// and the second picture is read as the stencil it is: a pixel it marks is one that is not there.

int ReadLe16(const unsigned char* data) {
    return data[0] | (data[1] << 8);
}

unsigned int ReadLe32(const unsigned char* data) {
    return static_cast<unsigned int>(data[0]) | (static_cast<unsigned int>(data[1]) << 8) |
           (static_cast<unsigned int>(data[2]) << 16) | (static_cast<unsigned int>(data[3]) << 24);
}

void WriteLe32(unsigned char* data, unsigned int value) {
    data[0] = static_cast<unsigned char>(value & 0xFFu);
    data[1] = static_cast<unsigned char>((value >> 8) & 0xFFu);
    data[2] = static_cast<unsigned char>((value >> 16) & 0xFFu);
    data[3] = static_cast<unsigned char>((value >> 24) & 0xFFu);
}

constexpr int kIconHeader = 6;
constexpr int kIconEntry = 16;
constexpr int kBmpFileHeader = 14;
constexpr int kMaxEdge = 8192;

// The picture in the icon worth drawing: the largest one, since every picture in an icon is the same
// picture at another size. A picture whose bytes fall outside the file is passed over rather than
// read, so a file that names a picture it does not carry still gives whatever else it does carry.
bool ChoosePicture(const unsigned char* data, int size, int count, int& offset, int& bytes, int& width, int& height) {
    offset = 0;
    bytes = 0;
    width = 0;
    height = 0;
    long long best = -1;
    for (int index = 0; index < count; ++index) {
        const unsigned char* entry = data + kIconHeader + index * kIconEntry;
        const int entryWidth = entry[0] == 0 ? 256 : entry[0];
        const int entryHeight = entry[1] == 0 ? 256 : entry[1];
        const int entryBytes = static_cast<int>(ReadLe32(entry + 8));
        const int entryOffset = static_cast<int>(ReadLe32(entry + 12));
        if (entryBytes < 1 || entryOffset < 0 || static_cast<long long>(entryOffset) + entryBytes > size) {
            continue;
        }
        if (entryWidth > kMaxEdge || entryHeight > kMaxEdge) {
            continue;
        }
        const long long area = static_cast<long long>(entryWidth) * static_cast<long long>(entryHeight);
        if (area > best) {
            best = area;
            offset = entryOffset;
            bytes = entryBytes;
            width = entryWidth;
            height = entryHeight;
        }
    }
    return best >= 0;
}

// Gives a bitmap that was written without a file header the header it is missing, so the reader
// written for bitmaps can read it. Two things have to be said for it: where the pixels begin, and
// that the height covers only the colours rather than the colours and the stencil together.
bool WrapBitmap(const unsigned char* payload, int payloadBytes, std::vector<unsigned char>& file) {
    if (payloadBytes < kBmpFileHeader + 40) {
        return false;
    }
    const unsigned int headerSize = ReadLe32(payload);
    const int bitCount = ReadLe16(payload + 14);
    if (headerSize < 40 || static_cast<long long>(headerSize) > payloadBytes) {
        return false;
    }
    const int colorCount = static_cast<int>(ReadLe32(payload + 32));
    const int paletteEntries = colorCount > 0 ? colorCount : (bitCount <= 8 ? (1 << bitCount) : 0);
    const long long pixelsOffset = static_cast<long long>(headerSize) + static_cast<long long>(paletteEntries) * 4;
    if (pixelsOffset > payloadBytes) {
        return false;
    }
    // The stencil is the bottom half of the picture the header describes, so the header is copied
    // with half the height written into it, which is the number of rows of colour there are.
    const int doubledHeight = static_cast<int>(ReadLe32(payload + 8));
    const int colorHeight = doubledHeight / 2;
    if (colorHeight < 1 || colorHeight > kMaxEdge) {
        return false;
    }
    file.assign(static_cast<std::size_t>(kBmpFileHeader) + static_cast<std::size_t>(payloadBytes), 0);
    file[0] = 'B';
    file[1] = 'M';
    WriteLe32(file.data() + 2, static_cast<unsigned int>(file.size()));
    WriteLe32(file.data() + 10, static_cast<unsigned int>(kBmpFileHeader + pixelsOffset));
    std::memcpy(file.data() + kBmpFileHeader, payload, static_cast<std::size_t>(payloadBytes));
    WriteLe32(file.data() + kBmpFileHeader + 8, static_cast<unsigned int>(colorHeight));
    return true;
}

// The stencil of a bitmap that kept transparency beside its colours rather than in them: one bit a
// pixel, written bottom up like the colours are, and a marked pixel is a pixel that is not there.
//
// A picture of thirty-two bits a pixel carries transparency in a channel of its own, so its stencil
// is read only where that channel says nothing at all, which is what a file written by a program
// that left the channel nought looks like. A picture of fewer bits has no channel to say it in, so
// its stencil is the only place its transparency can be and is always read. A picture that keeps
// its colours in a table has one entry a colour, and one that does not has none: reading the table
// as though it were there moves the stencil, which is how a palette is read into transparency that
// was never written.
bool ApplyStencil(const unsigned char* payload, int payloadBytes, int width, int height, int bitCount, ImageFrame& frame) {
    if (bitCount >= 32) {
        bool anyAlpha = false;
        for (std::size_t at = 3; at < frame.bgra.size(); at += 4) {
            if (frame.bgra[at] != 0) {
                anyAlpha = true;
                break;
            }
        }
        if (anyAlpha) {
            return true;
        }
    }
    const unsigned int headerSize = ReadLe32(payload);
    const int colorCount = static_cast<int>(ReadLe32(payload + 32));
    const int paletteEntries = colorCount > 0 ? colorCount : (bitCount <= 8 ? (1 << bitCount) : 0);
    const long long rowBytes = ((static_cast<long long>(width) * bitCount + 31) / 32) * 4;
    const long long colors = static_cast<long long>(headerSize) + static_cast<long long>(paletteEntries) * 4 + rowBytes * height;
    if (colors > payloadBytes) {
        return true;
    }
    const long long stencilRowBytes = ((static_cast<long long>(width) + 31) / 32) * 4;
    if (colors + stencilRowBytes * height > payloadBytes) {
        return true;
    }
    const unsigned char* stencil = payload + colors;
    for (int row = 0; row < height; ++row) {
        const int sourceRow = height - 1 - row;
        const unsigned char* line = stencil + static_cast<long long>(sourceRow) * stencilRowBytes;
        for (int column = 0; column < width; ++column) {
            if (((line[column / 8] >> (7 - (column % 8))) & 1) != 0) {
                const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)) * 4u;
                if (at + 3 < frame.bgra.size()) {
                    frame.bgra[at + 3] = 0;
                }
            }
        }
    }
    return true;
}

}  // namespace

bool DecodeIcoImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < kIconHeader + kIconEntry) {
        return false;
    }
    const int reserved = ReadLe16(data);
    const int type = ReadLe16(data + 2);
    const int count = ReadLe16(data + 4);
    if (reserved != 0 || (type != 1 && type != 2) || count < 1) {
        return false;
    }
    if (static_cast<long long>(kIconHeader) + static_cast<long long>(count) * kIconEntry > size) {
        return false;
    }
    int offset = 0;
    int bytes = 0;
    int width = 0;
    int height = 0;
    if (!ChoosePicture(data, size, count, offset, bytes, width, height)) {
        return false;
    }
    const unsigned char* payload = data + offset;
    // A picture that is a whole PNG inside is the PNG it is, read by the reader written for one.
    if (bytes >= 8 && std::memcmp(payload, "\x89PNG\r\n\x1a\n", 8) == 0) {
        DecodedImage png;
        if (!DecodePngImage(payload, bytes, png) || png.frames.empty()) {
            return false;
        }
        image.format = "ico";
        image.frames = std::move(png.frames);
        return true;
    }
    std::vector<unsigned char> file;
    if (!WrapBitmap(payload, bytes, file)) {
        return false;
    }
    DecodedImage bitmap;
    if (!DecodeBmpImage(file.data(), static_cast<int>(file.size()), bitmap) || bitmap.frames.empty()) {
        return false;
    }
    const int bitCount = ReadLe16(payload + 14);
    if (!ApplyStencil(payload, bytes, width, height, bitCount, bitmap.frames.front())) {
        return false;
    }
    image.format = "ico";
    image.frames = std::move(bitmap.frames);
    return true;
}

}  // namespace Runtime
