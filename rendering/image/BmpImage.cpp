#include "rendering/image/ImageCodec.h"

#include <cstring>
#include <vector>

namespace Runtime {
namespace {

// A bitmap is the plainest picture there is: a header that says how wide, how tall and how many
// bits a pixel is, a table of colours when the pixels are too few bits to name one themselves, and
// the rows of pixels after it. Every reading of it is portable, so a bitmap is drawn the same way
// wherever the runtime runs.
//
// The rows are written bottom up unless the height is negative, and each one is padded to a whole
// number of four-byte words, so a row is read for the width it covers and its padding is stepped
// over. A pixel is written in the order the channels are named in, which is blue first, and that is
// the order the runtime hands every picture over in, so nothing has to be reordered.

int ReadLe16(const unsigned char* data) {
    return data[0] | (data[1] << 8);
}

unsigned int ReadLe32(const unsigned char* data) {
    return static_cast<unsigned int>(data[0]) | (static_cast<unsigned int>(data[1]) << 8) |
           (static_cast<unsigned int>(data[2]) << 16) | (static_cast<unsigned int>(data[3]) << 24);
}

// One channel of a pixel taken from a field that need not be a whole byte wide: a channel written in
// five bits, or in six, is spread over the eight bits a channel is handed over in, so a picture that
// keeps its colours in less room than a channel has is drawn as brightly as it was meant to be.
int ScaleField(unsigned int value, unsigned int mask) {
    if (mask == 0) {
        return 255;
    }
    int shift = 0;
    while (shift < 32 && (mask & (1u << shift)) == 0) {
        ++shift;
    }
    const unsigned int field = mask >> shift;
    if (field == 0) {
        return 0;
    }
    const unsigned int narrowed = (value & mask) >> shift;
    return static_cast<int>((narrowed * 255u + field / 2u) / field);
}

// The number of colours a file states when it keeps fewer bits than a byte for a pixel, which is
// the size of the table that follows its header. A file that states none is read as the table the
// number of bits itself implies.
int PaletteCount(const unsigned char* data, int bitCount) {
    const int stated = static_cast<int>(ReadLe32(data + 46));
    if (stated > 0) {
        return stated;
    }
    return bitCount <= 8 ? (1 << bitCount) : 0;
}

}  // namespace

bool DecodeBmpImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 26 || data[0] != 'B' || data[1] != 'M') {
        return false;
    }
    const unsigned int dataOffset = ReadLe32(data + 10);
    const unsigned int headerSize = ReadLe32(data + 14);
    if (headerSize < 40 || 14 + static_cast<long long>(headerSize) > size) {
        return false;
    }
    const int width = static_cast<int>(ReadLe32(data + 18));
    int height = static_cast<int>(ReadLe32(data + 22));
    const int planes = ReadLe16(data + 26);
    const int bitCount = ReadLe16(data + 28);
    const unsigned int compression = ReadLe32(data + 30);
    if (width < 1 || height == 0 || width > 8192 || planes != 1) {
        return false;
    }
    // A negative height is not a height below nothing: it says the rows were written top down,
    // which is the one thing about the picture the sign is kept for.
    const bool topDown = height < 0;
    if (height < 0) {
        height = -height;
    }
    if (height > 8192) {
        return false;
    }
    if (bitCount != 1 && bitCount != 4 && bitCount != 8 && bitCount != 16 && bitCount != 24 && bitCount != 32) {
        return false;
    }
    // The plain form, the form that names its channel masks, and the form that names them and keeps
    // transparency as well. Every other form is a compression the runtime does not read.
    if (compression != 0 && compression != 3 && compression != 6) {
        return false;
    }
    if (compression != 0 && bitCount != 16 && bitCount != 32) {
        return false;
    }
    const int colorCount = PaletteCount(data, bitCount);
    const int paletteOffset = 14 + static_cast<int>(headerSize);
    if (colorCount < 0 || static_cast<long long>(paletteOffset) + static_cast<long long>(colorCount) * 4 > size) {
        return false;
    }
    unsigned int redMask = 0;
    unsigned int greenMask = 0;
    unsigned int blueMask = 0;
    unsigned int alphaMask = 0;
    if (bitCount == 16 || bitCount == 32) {
        if (compression == 3 || compression == 6) {
            const int maskOffset = 14 + 40;
            if (maskOffset + 12 > size) {
                return false;
            }
            redMask = ReadLe32(data + maskOffset);
            greenMask = ReadLe32(data + maskOffset + 4);
            blueMask = ReadLe32(data + maskOffset + 8);
            if (maskOffset + 16 <= size) {
                alphaMask = ReadLe32(data + maskOffset + 12);
            }
        } else if (bitCount == 16) {
            redMask = 0x7C00u;
            greenMask = 0x03E0u;
            blueMask = 0x001Fu;
        } else {
            blueMask = 0x000000FFu;
            greenMask = 0x0000FF00u;
            redMask = 0x00FF0000u;
            alphaMask = 0xFF000000u;
        }
    }
    const long long rowBytes = ((static_cast<long long>(width) * bitCount + 31) / 32) * 4;
    if (static_cast<long long>(dataOffset) + rowBytes * height > size) {
        return false;
    }
    ImageFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    bool sawAlpha = false;
    for (int row = 0; row < height; ++row) {
        const int sourceRow = topDown ? row : height - 1 - row;
        const unsigned char* line = data + dataOffset + static_cast<long long>(sourceRow) * rowBytes;
        for (int column = 0; column < width; ++column) {
            int blue = 0;
            int green = 0;
            int red = 0;
            int alpha = 255;
            if (bitCount <= 8) {
                int index = 0;
                if (bitCount == 1) {
                    index = (line[column / 8] >> (7 - (column % 8))) & 1;
                } else if (bitCount == 4) {
                    index = (line[column / 2] >> ((column % 2) == 0 ? 4 : 0)) & 15;
                } else {
                    index = line[column];
                }
                if (index >= colorCount) {
                    return false;
                }
                const unsigned char* entry = data + paletteOffset + index * 4;
                blue = entry[0];
                green = entry[1];
                red = entry[2];
            } else if (bitCount == 24) {
                const unsigned char* pixel = line + column * 3;
                blue = pixel[0];
                green = pixel[1];
                red = pixel[2];
            } else {
                const unsigned int value =
                    bitCount == 16 ? (static_cast<unsigned int>(line[column * 2]) | (static_cast<unsigned int>(line[column * 2 + 1]) << 8))
                                   : ReadLe32(line + column * 4);
                blue = ScaleField(value, blueMask);
                green = ScaleField(value, greenMask);
                red = ScaleField(value, redMask);
                if (alphaMask != 0) {
                    alpha = ScaleField(value, alphaMask);
                    if (alpha != 0) {
                        sawAlpha = true;
                    }
                }
            }
            const std::size_t at = (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(column)) * 4u;
            frame.bgra[at] = static_cast<unsigned char>(blue);
            frame.bgra[at + 1] = static_cast<unsigned char>(green);
            frame.bgra[at + 2] = static_cast<unsigned char>(red);
            frame.bgra[at + 3] = static_cast<unsigned char>(alpha);
        }
    }
    if (bitCount == 32 && compression == 0 && !sawAlpha) {
        // The plain form of a thirty-two bit bitmap names no transparency, and the far byte of every
        // pixel in one that has none written in it is nought. A picture where every pixel is written
        // that way is opaque: the byte was never a channel, so nothing in it should make the picture
        // vanish.
        for (std::size_t at = 3; at < frame.bgra.size(); at += 4) {
            frame.bgra[at] = 255;
        }
    }
    image.format = "bmp";
    image.frames.push_back(std::move(frame));
    return true;
}

}  // namespace Runtime
