#include "rendering/image/ImageCodec.h"

#include "rendering/image/Inflate.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace Runtime {
namespace {

constexpr int kMaxEdge = 8192;

int ReadBe32(const unsigned char* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

int ReadBe16(const unsigned char* data) {
    return (data[0] << 8) | data[1];
}

int Paeth(int left, int up, int upLeft) {
    const int estimate = left + up - upLeft;
    const int leftDistance = std::abs(estimate - left);
    const int upDistance = std::abs(estimate - up);
    const int diagonal = std::abs(estimate - upLeft);
    if (leftDistance <= upDistance && leftDistance <= diagonal) {
        return left;
    }
    if (upDistance <= diagonal) {
        return up;
    }
    return upLeft;
}

void PutBgra(std::vector<unsigned char>& pixels, int index, int blue, int green, int red, int alpha) {
    pixels[static_cast<std::size_t>(index)] = static_cast<unsigned char>(blue);
    pixels[static_cast<std::size_t>(index + 1)] = static_cast<unsigned char>(green);
    pixels[static_cast<std::size_t>(index + 2)] = static_cast<unsigned char>(red);
    pixels[static_cast<std::size_t>(index + 3)] = static_cast<unsigned char>(alpha);
}

bool Unfilter(const std::vector<unsigned char>& raw, int width, int height, int channels, int bitDepth,
              std::vector<unsigned char>& samples) {
    const int depthBytes = bitDepth == 16 ? 2 : 1;
    const int pixelBytes = channels * depthBytes;
    const int rowBytes = (width * channels * bitDepth + 7) / 8;
    if (static_cast<int>(raw.size()) < height * (rowBytes + 1)) {
        return false;
    }
    samples.assign(static_cast<std::size_t>(height * rowBytes), 0);
    for (int y = 0; y < height; ++y) {
        const int filter = raw[static_cast<std::size_t>(y * (rowBytes + 1))];
        const unsigned char* source = raw.data() + static_cast<std::size_t>(y * (rowBytes + 1) + 1);
        unsigned char* dest = samples.data() + static_cast<std::size_t>(y * rowBytes);
        const unsigned char* prior = y > 0 ? dest - rowBytes : nullptr;
        for (int x = 0; x < rowBytes; ++x) {
            const int left = x >= pixelBytes ? dest[x - pixelBytes] : 0;
            const int up = prior != nullptr ? prior[x] : 0;
            const int upLeft = prior != nullptr && x >= pixelBytes ? prior[x - pixelBytes] : 0;
            const int value = source[x];
            int decoded = value;
            if (filter == 1) {
                decoded = value + left;
            } else if (filter == 2) {
                decoded = value + up;
            } else if (filter == 3) {
                decoded = value + ((left + up) / 2);
            } else if (filter == 4) {
                decoded = value + Paeth(left, up, upLeft);
            } else if (filter != 0) {
                return false;
            }
            dest[x] = static_cast<unsigned char>(decoded & 255);
        }
    }
    return true;
}

int SampleBits(const unsigned char* row, int bitDepth, int index) {
    if (bitDepth == 16) {
        return (row[index * 2] << 8) | row[index * 2 + 1];
    }
    if (bitDepth == 8) {
        return row[index];
    }
    const int packed = 8 / bitDepth;
    const int shift = (packed - 1 - (index % packed)) * bitDepth;
    return (row[index / packed] >> shift) & ((1 << bitDepth) - 1);
}

int ScaleSample(int value, int bitDepth) {
    if (bitDepth == 16) {
        return value >> 8;
    }
    if (bitDepth == 8) {
        return value;
    }
    const int maxValue = (1 << bitDepth) - 1;
    return maxValue == 0 ? 0 : value * 255 / maxValue;
}

void WritePngPixel(std::vector<unsigned char>& pixels, int x, int y, int width, int red, int green, int blue, int alpha) {
    if (x < 0 || y < 0 || x >= width) {
        return;
    }
    PutBgra(pixels, (y * width + x) * 4, blue, green, red, alpha);
}

bool ExpandPng(const std::vector<unsigned char>& samples, int width, int height, int colorType, int bitDepth,
               const std::vector<unsigned char>& palette, const std::vector<unsigned char>& transparency,
               std::vector<unsigned char>& pixels) {
    const int channels = colorType == 2 ? 3 : colorType == 6 ? 4 : colorType == 4 ? 2 : 1;
    const int rowBytes = (width * channels * bitDepth + 7) / 8;
    pixels.assign(static_cast<std::size_t>(width * height * 4), 0);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = samples.data() + static_cast<std::size_t>(y * rowBytes);
        for (int x = 0; x < width; ++x) {
            int red = 0;
            int green = 0;
            int blue = 0;
            int alpha = 255;
            if (colorType == 2 || colorType == 6) {
                red = ScaleSample(SampleBits(row, bitDepth, x * channels), bitDepth);
                green = ScaleSample(SampleBits(row, bitDepth, x * channels + 1), bitDepth);
                blue = ScaleSample(SampleBits(row, bitDepth, x * channels + 2), bitDepth);
                if (colorType == 6) {
                    alpha = ScaleSample(SampleBits(row, bitDepth, x * channels + 3), bitDepth);
                }
            } else if (colorType == 0 || colorType == 4) {
                const int gray = ScaleSample(SampleBits(row, bitDepth, x * channels), bitDepth);
                red = green = blue = gray;
                if (colorType == 4) {
                    alpha = ScaleSample(SampleBits(row, bitDepth, x * channels + 1), bitDepth);
                } else if (!transparency.empty() && transparency.size() >= 2) {
                    const int key = (transparency[0] << 8) | transparency[1];
                    if (SampleBits(row, bitDepth, x) == key) {
                        alpha = 0;
                    }
                }
            } else if (colorType == 3) {
                const int index = SampleBits(row, bitDepth, x);
                const int entry = index * 3;
                if (entry + 2 >= static_cast<int>(palette.size())) {
                    return false;
                }
                red = palette[static_cast<std::size_t>(entry)];
                green = palette[static_cast<std::size_t>(entry + 1)];
                blue = palette[static_cast<std::size_t>(entry + 2)];
                if (index < static_cast<int>(transparency.size())) {
                    alpha = transparency[static_cast<std::size_t>(index)];
                }
            } else {
                return false;
            }
            WritePngPixel(pixels, x, y, width, red, green, blue, alpha);
        }
    }
    return true;
}

const int kAdam7RowStart[7] = {0, 0, 4, 0, 2, 0, 1};
const int kAdam7ColStart[7] = {0, 4, 0, 2, 0, 1, 0};
const int kAdam7RowStep[7] = {8, 8, 8, 4, 4, 2, 2};
const int kAdam7ColStep[7] = {8, 8, 4, 4, 2, 2, 1};

bool DecodePngPass(const std::vector<unsigned char>& inflated, int& cursor, int passWidth, int passHeight, int channels, int bitDepth,
                   std::vector<unsigned char>& samples) {
    const int rowBytes = (passWidth * channels * bitDepth + 7) / 8;
    const int need = passHeight * (rowBytes + 1);
    if (passWidth <= 0 || passHeight <= 0) {
        return true;
    }
    if (cursor + need > static_cast<int>(inflated.size())) {
        return false;
    }
    std::vector<unsigned char> raw(inflated.begin() + cursor, inflated.begin() + cursor + need);
    cursor += need;
    return Unfilter(raw, passWidth, passHeight, channels, bitDepth, samples);
}

struct JpegBit {
    const unsigned char* data = nullptr;
    int size = 0;
    int index = 0;
    unsigned int buffer = 0;
    int bits = 0;

    int NextByte() {
        if (index >= size) {
            return -1;
        }
        int value = data[index++];
        if (value == 0xFF) {
            if (index >= size) {
                return -1;
            }
            const int marker = data[index++];
            if (marker == 0x00) {
                return 0xFF;
            }
            index -= 2;
            return -1;
        }
        return value;
    }

    int Bit() {
        if (bits == 0) {
            const int value = NextByte();
            if (value < 0) {
                return -1;
            }
            buffer = static_cast<unsigned int>(value);
            bits = 8;
        }
        --bits;
        return static_cast<int>((buffer >> bits) & 1u);
    }

    int Bits(int count) {
        int value = 0;
        for (int index = 0; index < count; ++index) {
            const int bit = Bit();
            if (bit < 0) {
                return -1;
            }
            value = (value << 1) | bit;
        }
        return value;
    }

    // Steps over a restart marker at the place in a scan where one is due. Whatever is left of the
    // byte in hand is dropped, because a restart marker begins on a byte boundary, and the bits the
    // writer padded with to reach that boundary are not bits of any coefficient.
    void Restart() {
        bits = 0;
        buffer = 0;
        while (index < size && data[index] != 0xFF) {
            ++index;
        }
        while (index + 1 < size) {
            const int marker = data[index + 1];
            if (marker >= 0xD0 && marker <= 0xD7) {
                index += 2;
                return;
            }
            if (marker == 0xFF) {
                ++index;
                continue;
            }
            return;
        }
    }
};

struct JpegHuffman {
    int counts[16] = {};
    int minCode[16] = {};
    int maxCode[16] = {};
    int offset[16] = {};
    int symbols[256] = {};

    bool Build(const int* lengths, const int* values, int valueCount) {
        int blCount[16] = {};
        for (int symbol = 0; symbol < 16; ++symbol) {
            blCount[symbol] = lengths[symbol];
        }
        int code = 0;
        int cursor = 0;
        for (int length = 1; length <= 16; ++length) {
            minCode[length - 1] = code;
            maxCode[length - 1] = code + blCount[length - 1] - 1;
            offset[length - 1] = cursor;
            counts[length - 1] = blCount[length - 1];
            cursor += blCount[length - 1];
            code = (code + blCount[length - 1]) << 1;
        }
        if (cursor > valueCount || cursor > 256) {
            return false;
        }
        for (int index = 0; index < cursor; ++index) {
            symbols[index] = values[index];
        }
        return true;
    }

    int Decode(JpegBit& input) const {
        int code = 0;
        for (int length = 0; length < 16; ++length) {
            const int bit = input.Bit();
            if (bit < 0) {
                return -1;
            }
            code = (code << 1) | bit;
            if (counts[length] > 0 && code <= maxCode[length]) {
                return symbols[offset[length] + (code - minCode[length])];
            }
        }
        return -1;
    }
};

const int kZigZag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                         41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                         30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

void InverseDct(const int input[64], int output[64]) {
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            double sum = 0.0;
            for (int v = 0; v < 8; ++v) {
                for (int u = 0; u < 8; ++u) {
                    const double cu = u == 0 ? 0.7071067811865476 : 1.0;
                    const double cv = v == 0 ? 0.7071067811865476 : 1.0;
                    sum += cu * cv * input[v * 8 + u] * std::cos((2.0 * x + 1.0) * u * 3.141592653589793 / 16.0) *
                           std::cos((2.0 * y + 1.0) * v * 3.141592653589793 / 16.0);
                }
            }
            output[y * 8 + x] = static_cast<int>(std::lround(sum / 4.0));
        }
    }
}

int Extend(int magnitude, int count) {
    if (count == 0) {
        return 0;
    }
    const int threshold = 1 << (count - 1);
    if (magnitude < threshold) {
        return magnitude - ((1 << count) - 1);
    }
    return magnitude;
}

int ClampByte(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

struct GifReader {
    const unsigned char* data = nullptr;
    int size = 0;
    int index = 0;

    int Byte() {
        if (index >= size) {
            return -1;
        }
        return data[index++];
    }

    bool Skip(int count) {
        if (count < 0 || index + count > size) {
            return false;
        }
        index += count;
        return true;
    }
};

bool ReadGifPalette(GifReader& reader, int count, std::vector<unsigned char>& palette) {
    palette.resize(static_cast<std::size_t>(count * 3));
    for (int index = 0; index < count * 3; ++index) {
        const int value = reader.Byte();
        if (value < 0) {
            return false;
        }
        palette[static_cast<std::size_t>(index)] = static_cast<unsigned char>(value);
    }
    return true;
}

// The codes of a picture: the bits of the file packed least significant bit first, and the bytes
// themselves arriving in runs that each state their own length.
//
// A run is read a byte at a time as the codes need it rather than held whole, because a run is as
// long as two hundred and fifty-five bytes while a code is no more than twelve bits. Gathering a
// whole run to take twelve bits of it puts a thousand bits into a register that holds thirty-two,
// which is how the bits after the first four bytes of a long run are lost.
struct GifCodes {
    GifReader* reader = nullptr;
    unsigned int buffer = 0;
    int bits = 0;
    int remaining = 0;
    bool ended = false;

    // One more byte in the register, or false when the runs have ended. A run ends at a block of no
    // length, which is also where the picture ends, so nothing is read past it by accident.
    bool Fill() {
        while (bits <= 24) {
            if (remaining == 0) {
                if (ended) {
                    return false;
                }
                const int block = reader->Byte();
                if (block <= 0) {
                    ended = true;
                    return false;
                }
                remaining = block;
            }
            const int value = reader->Byte();
            if (value < 0) {
                ended = true;
                return false;
            }
            --remaining;
            buffer |= static_cast<unsigned int>(value) << bits;
            bits += 8;
        }
        return true;
    }

    // The next code of this many bits, or nothing when the codes have run out. The register is
    // filled only until it can pay for the code, and filling stopping short is not the codes
    // running out: a short file ends its run while the register still holds the bits read before it,
    // so what is already there is spent before the stream is called finished.
    int Take(int count) {
        if (bits < count) {
            Fill();
        }
        if (bits < count) {
            return -1;
        }
        const int value = static_cast<int>(buffer & ((1u << count) - 1u));
        buffer >>= count;
        bits -= count;
        return value;
    }

    // Steps over whatever is left of the runs the codes came in, so the reader is left at the byte
    // after the picture rather than somewhere inside it.
    void Drain() {
        if (ended) {
            return;
        }
        if (remaining > 0) {
            reader->Skip(remaining);
            remaining = 0;
        }
        while (true) {
            const int block = reader->Byte();
            if (block <= 0) {
                return;
            }
            if (!reader->Skip(block)) {
                return;
            }
        }
    }
};

bool DecodeGifLzw(GifReader& reader, int minimumCode, int pixelCount, std::vector<unsigned char>& indices) {
    const int clear = 1 << minimumCode;
    const int end = clear + 1;
    int codeSize = minimumCode + 1;
    int next = end + 1;
    std::vector<int> prefix(4096);
    std::vector<unsigned char> suffix(4096);
    for (int code = 0; code < clear; ++code) {
        suffix[static_cast<std::size_t>(code)] = static_cast<unsigned char>(code);
    }
    GifCodes codes;
    codes.reader = &reader;
    int previous = -1;
    indices.clear();
    std::vector<unsigned char> sequence;
    while (static_cast<int>(indices.size()) < pixelCount) {
        const int code = codes.Take(codeSize);
        if (code < 0) {
            break;
        }
        if (code == clear) {
            codeSize = minimumCode + 1;
            next = end + 1;
            previous = -1;
            continue;
        }
        if (code == end) {
            break;
        }
        sequence.clear();
        if (code < next) {
            int current = code;
            while (current >= clear) {
                sequence.push_back(suffix[static_cast<std::size_t>(current)]);
                current = prefix[static_cast<std::size_t>(current)];
            }
            sequence.push_back(suffix[static_cast<std::size_t>(current)]);
        } else if (code == next && previous >= 0) {
            // The one code a picture may use before it has been written: the string of the code
            // before it, followed by the first character of that same string.
            int current = previous;
            while (current >= clear) {
                sequence.push_back(suffix[static_cast<std::size_t>(current)]);
                current = prefix[static_cast<std::size_t>(current)];
            }
            sequence.push_back(suffix[static_cast<std::size_t>(current)]);
            sequence.insert(sequence.begin(), sequence.back());
        } else {
            break;
        }
        for (int index = static_cast<int>(sequence.size()) - 1; index >= 0; --index) {
            indices.push_back(sequence[static_cast<std::size_t>(index)]);
        }
        if (previous >= 0 && next < 4096) {
            prefix[static_cast<std::size_t>(next)] = previous;
            int head = previous;
            while (head >= clear) {
                head = prefix[static_cast<std::size_t>(head)];
            }
            suffix[static_cast<std::size_t>(next)] = suffix[static_cast<std::size_t>(head)];
            if (code == next) {
                suffix[static_cast<std::size_t>(next)] = sequence.back();
            }
            ++next;
            // The codes grow one bit wider as the table fills, at the size where the next code to be
            // written would no longer fit in the bits the picture is being read with.
            if (next == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }
        previous = code;
    }
    codes.Drain();
    // A picture whose codes run out before every pixel has been written is kept for the pixels it
    // did write rather than refused: a file is written by encoders that stop early on a frame whose
    // tail is not worth the bytes, and every reader a person has seen draws such a frame with the
    // part that is there. Only a picture that produced nothing at all is a picture that failed.
    return !indices.empty();
}

// The rows a picture's rows are drawn at. A picture stored in order is drawn in order; one stored
// interlaced is drawn in four passes, every eighth row and then every eighth starting four rows
// down, then every fourth, then every second, so a picture half-drawn is a picture rather than a
// band of one.
std::vector<int> GifRowOrder(int height, bool interlaced) {
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(height));
    if (!interlaced) {
        for (int y = 0; y < height; ++y) {
            order.push_back(y);
        }
        return order;
    }
    for (int pass = 0; pass < 4; ++pass) {
        const int start = pass == 0 ? 0 : pass == 1 ? 4 : pass == 2 ? 2 : 1;
        const int step = pass < 2 ? 8 : pass == 2 ? 4 : 2;
        for (int y = start; y < height; y += step) {
            order.push_back(y);
        }
    }
    return order;
}

// The pixels of one PNG image: the compressed bytes of its rows, and the frame they belong to. A
// still picture is one of these. An animation is one of these for every frame of it, each written as
// its own run of rows and laid at its own place in the picture the frames make together, which is
// why the rows are read against the size the frame itself states rather than the size of the file.
bool DecodePngPixels(const std::vector<unsigned char>& compressed, int width, int height, int bitDepth, int colorType,
                     int interlace, const std::vector<unsigned char>& palette,
                     const std::vector<unsigned char>& transparency, std::vector<unsigned char>& pixels) {
    if (width < 1 || height < 1 || width > kMaxEdge || height > kMaxEdge) {
        return false;
    }
    const int channels = colorType == 2 ? 3 : colorType == 6 ? 4 : colorType == 4 ? 2 : 1;
    std::vector<unsigned char> inflated;
    if (!InflateZlib(compressed.data(), static_cast<int>(compressed.size()), inflated)) {
        return false;
    }
    pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    if (interlace == 0) {
        std::vector<unsigned char> samples;
        return Unfilter(inflated, width, height, channels, bitDepth, samples) &&
               ExpandPng(samples, width, height, colorType, bitDepth, palette, transparency, pixels);
    }
    if (interlace != 1) {
        return false;
    }
    int offset = 0;
    for (int pass = 0; pass < 7; ++pass) {
        const int passWidth = (width - kAdam7ColStart[pass] + kAdam7ColStep[pass] - 1) / kAdam7ColStep[pass];
        const int passHeight = (height - kAdam7RowStart[pass] + kAdam7RowStep[pass] - 1) / kAdam7RowStep[pass];
        if (passWidth <= 0 || passHeight <= 0) {
            continue;
        }
        std::vector<unsigned char> samples;
        if (!DecodePngPass(inflated, offset, passWidth, passHeight, channels, bitDepth, samples)) {
            return false;
        }
        std::vector<unsigned char> passPixels;
        if (!ExpandPng(samples, passWidth, passHeight, colorType, bitDepth, palette, transparency, passPixels)) {
            return false;
        }
        int row = 0;
        for (int y = kAdam7RowStart[pass]; y < height; y += kAdam7RowStep[pass], ++row) {
            int column = 0;
            for (int x = kAdam7ColStart[pass]; x < width; x += kAdam7ColStep[pass], ++column) {
                const int source = (row * passWidth + column) * 4;
                const int dest = (y * width + x) * 4;
                for (int channel = 0; channel < 4; ++channel) {
                    pixels[static_cast<std::size_t>(dest + channel)] = passPixels[static_cast<std::size_t>(source + channel)];
                }
            }
        }
    }
    return true;
}

// One frame of an animation, as the file describes it: where it sits in the picture the frames make
// up between them, how long it is shown for, and what is done to the pixels it covers once it is
// gone. The two ways of laying a frame down are replacing what is under it and drawing over it; the
// two ways of clearing up after one are putting the pixels back as they were and wiping the place
// bare, which is what makes a frame with a hole in it possible.
struct PngFrame {
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
    int delayMs = 100;
    int dispose = 0;
    int blend = 0;
    bool usesImageData = false;
    std::vector<unsigned char> bytes;
};

// How long a frame is held for. The file states it as a fraction of a second, and a file that states
// no denominator is stating hundredths, which is the same thing as the fractions a browser reads.
int PngFrameDelay(int numerator, int denominator) {
    if (numerator < 1) {
        return 100;
    }
    if (denominator < 1) {
        denominator = 100;
    }
    const int milliseconds = static_cast<int>(static_cast<long long>(numerator) * 1000 / denominator);
    return milliseconds < 1 ? 1 : milliseconds;
}

}  // namespace

bool DecodePngImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 8 || std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0) {
        return false;
    }
    int width = 0;
    int height = 0;
    int bitDepth = 0;
    int colorType = -1;
    int interlace = 0;
    int statedFrames = 0;
    std::vector<unsigned char> idat;
    std::vector<unsigned char> palette;
    std::vector<unsigned char> transparency;
    std::vector<PngFrame> frames;
    bool controlSeen = false;
    bool controlBeforeData = false;
    int cursor = 8;
    while (cursor + 12 <= size) {
        const int length = ReadBe32(data + cursor);
        if (length < 0 || cursor + 12 + length > size) {
            return false;
        }
        const unsigned char* type = data + cursor + 4;
        const unsigned char* chunk = data + cursor + 8;
        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13) {
            width = ReadBe32(chunk);
            height = ReadBe32(chunk + 4);
            bitDepth = chunk[8];
            colorType = chunk[9];
            interlace = chunk[12];
        } else if (std::memcmp(type, "PLTE", 4) == 0) {
            palette.assign(chunk, chunk + length);
        } else if (std::memcmp(type, "tRNS", 4) == 0) {
            transparency.assign(chunk, chunk + length);
        } else if (std::memcmp(type, "acTL", 4) == 0 && length >= 8) {
            statedFrames = ReadBe32(chunk);
        } else if (std::memcmp(type, "fcTL", 4) == 0 && length >= 26) {
            PngFrame frame;
            frame.width = ReadBe32(chunk + 4);
            frame.height = ReadBe32(chunk + 8);
            frame.x = ReadBe32(chunk + 12);
            frame.y = ReadBe32(chunk + 16);
            frame.delayMs = PngFrameDelay((chunk[20] << 8) | chunk[21], (chunk[22] << 8) | chunk[23]);
            frame.dispose = chunk[24];
            frame.blend = chunk[25];
            if (!controlSeen) {
                controlSeen = true;
                // A file whose first description of a frame stands before the picture's own bytes is
                // describing them: the picture is the first frame of the animation. A file whose
                // first description stands after them is leaving them as a still for a reader that
                // reads no animation, and animating only the frames that follow.
                controlBeforeData = idat.empty();
            }
            frames.push_back(std::move(frame));
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunk, chunk + length);
        } else if (std::memcmp(type, "fdAT", 4) == 0 && length >= 4) {
            if (!frames.empty()) {
                frames.back().bytes.insert(frames.back().bytes.end(), chunk + 4, chunk + length);
            }
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        cursor += 12 + length;
    }
    if (width < 1 || height < 1 || width > kMaxEdge || height > kMaxEdge || colorType < 0) {
        return false;
    }
    const bool animated = statedFrames > 0 && !frames.empty();
    if (!animated) {
        ImageFrame frame;
        frame.width = width;
        frame.height = height;
        if (!DecodePngPixels(idat, width, height, bitDepth, colorType, interlace, palette, transparency, frame.bgra)) {
            return false;
        }
        image.format = "png";
        image.frames.push_back(std::move(frame));
        return true;
    }
    if (controlBeforeData && !frames.empty()) {
        frames.front().usesImageData = true;
    }
    // The frames are laid one after another on the picture they make together, and what each one
    // leaves behind decides what the next one starts from, so the canvas is carried between them.
    std::vector<unsigned char> canvas(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    for (std::size_t index = 0; index < frames.size(); ++index) {
        const PngFrame& frame = frames[index];
        const std::vector<unsigned char>& compressed = frame.usesImageData ? idat : frame.bytes;
        std::vector<unsigned char> laid;
        if (!DecodePngPixels(compressed, frame.width, frame.height, bitDepth, colorType, interlace, palette, transparency, laid)) {
            return false;
        }
        const std::vector<unsigned char> before = canvas;
        const int left = frame.x < 0 ? 0 : frame.x;
        const int top = frame.y < 0 ? 0 : frame.y;
        const int right = frame.x + frame.width > width ? width : frame.x + frame.width;
        const int bottom = frame.y + frame.height > height ? height : frame.y + frame.height;
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const std::size_t source =
                    (static_cast<std::size_t>(y - frame.y) * static_cast<std::size_t>(frame.width) + static_cast<std::size_t>(x - frame.x)) * 4u;
                const std::size_t dest = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
                if (frame.blend == 0) {
                    // A frame that replaces what is under it is written over it whole, transparency
                    // and all, which is what makes a frame with a hole in it possible.
                    for (int channel = 0; channel < 4; ++channel) {
                        canvas[dest + channel] = laid[source + channel];
                    }
                    continue;
                }
                // A frame drawn over what is under it keeps whatever is showing through it.
                const float sourceAlpha = static_cast<float>(laid[source + 3]) / 255.0f;
                if (sourceAlpha >= 1.0f) {
                    for (int channel = 0; channel < 4; ++channel) {
                        canvas[dest + channel] = laid[source + channel];
                    }
                    continue;
                }
                const float destAlpha = static_cast<float>(canvas[dest + 3]) / 255.0f;
                const float alpha = sourceAlpha + destAlpha * (1.0f - sourceAlpha);
                const float weight = alpha <= 0.0f ? 0.0f : 1.0f / alpha;
                for (int channel = 0; channel < 3; ++channel) {
                    const float value = (static_cast<float>(laid[source + channel]) * sourceAlpha +
                                         static_cast<float>(canvas[dest + channel]) * destAlpha * (1.0f - sourceAlpha)) *
                                        weight;
                    canvas[dest + channel] = static_cast<unsigned char>(value < 0.0f ? 0.0f : value > 255.0f ? 255.0f : value);
                }
                canvas[dest + 3] = static_cast<unsigned char>(alpha * 255.0f);
            }
        }
        ImageFrame shown;
        shown.width = width;
        shown.height = height;
        shown.delayMs = frame.delayMs;
        shown.bgra = canvas;
        image.frames.push_back(std::move(shown));
        if (frame.dispose == 2) {
            // A frame that puts things back leaves the canvas as it found it, which is how a frame
            // smaller than the picture is drawn over one that stands for longer.
            canvas = before;
        } else if (frame.dispose == 1) {
            // A frame that wipes its place bare leaves nothing at all behind it.
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const std::size_t dest = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
                    for (int channel = 0; channel < 4; ++channel) {
                        canvas[dest + channel] = 0;
                    }
                }
            }
        }
    }
    image.format = "png";
    return true;
}

// One component of a frame: how finely it is sampled, where its coefficients are kept, and the
// tables the scan being read draws them with. The coefficients are kept as they were coded rather
// than as the pixels they stand for, because a progressive file codes one band of a block at a time
// and a later scan may refine a band an earlier one already wrote: only once every scan has been
// read can a block be turned into pixels at all.
struct JpegComponent {
    int id = 0;
    int horizontal = 1;
    int vertical = 1;
    int quantTable = 0;
    int dcTable = 0;
    int acTable = 0;
    int prediction = 0;
    int blocksWide = 0;
    int blocksHigh = 0;
    // The blocks a band of one component is coded over, which is that component's own grid rather
    // than the grid of interleaved units. The two differ when a component is sampled less finely
    // than the frame, which is the case every colour picture is written in.
    int scanBlocksWide = 0;
    int scanBlocksHigh = 0;
    std::vector<int> coefficients;
};

// What one scan states: which components it holds coefficients for, which band of a block those
// coefficients lie in, and how far the approximation has come.
struct JpegScan {
    int componentCount = 0;
    int components[4] = {};
    int dcTable[4] = {};
    int acTable[4] = {};
    int spectralStart = 0;
    int spectralEnd = 63;
    int successiveHigh = 0;
    int successiveLow = 0;
};

// Everything a scan needs beyond its own header, held for the whole file because a progressive file
// writes its scans one after another and each one adds to what the last one left.
struct JpegFrame {
    int width = 0;
    int height = 0;
    int componentCount = 0;
    JpegComponent components[4];
    int maxHorizontal = 1;
    int maxVertical = 1;
    int restartInterval = 0;
    int quant[4][64] = {};
    bool quantUsed[4] = {};
    JpegHuffman dcTables[4];
    JpegHuffman acTables[4];
    bool dcReady[4] = {};
    bool acReady[4] = {};
};

// Where the coded data of a scan ends and the marker after it begins. A scan carries no length of
// its own: it ends at the first marker that is neither a restart marker nor a stuffed nought, so
// finding it means walking it rather than reading its size.
int SkipEntropyData(const unsigned char* data, int size, int from) {
    int at = from;
    while (at + 1 < size) {
        if (data[at] != 0xFF) {
            ++at;
            continue;
        }
        const int marker = data[at + 1];
        if (marker == 0xFF) {
            ++at;
            continue;
        }
        if (marker == 0x00 || (marker >= 0xD0 && marker <= 0xD7)) {
            at += 2;
            continue;
        }
        return at;
    }
    return size;
}

// One block of one scan.
//
// The first pass over a band reads the coefficients that band holds: a run of noughts and then a
// value, or a run that says the rest of the band is nought for this block and some number of the
// blocks after it. The refinement pass does not read values at all; it reads one bit for each
// coefficient the earlier pass left standing, which says whether that coefficient is one step
// closer to what it was written as, and at most one new coefficient per block.
bool JpegDecodeBlock(JpegBit& bits, JpegFrame& frame, JpegComponent& component, int blockIndex, const JpegScan& scan,
                     int& eobRun) {
    int* coefficients = component.coefficients.data() + static_cast<std::size_t>(blockIndex) * 64;
    if (scan.spectralStart == 0) {
        if (scan.successiveHigh == 0) {
            const int dcTable = component.dcTable;
            if (dcTable > 3 || !frame.dcReady[dcTable]) {
                return false;
            }
            const int symbol = frame.dcTables[dcTable].Decode(bits);
            if (symbol < 0) {
                return false;
            }
            const int magnitude = symbol == 0 ? 0 : bits.Bits(symbol);
            if (symbol != 0 && magnitude < 0) {
                return false;
            }
            component.prediction += Extend(magnitude, symbol);
            coefficients[0] = component.prediction * (1 << scan.successiveLow);
        } else {
            // A refinement of the first coefficient says only whether it is nearer its true value
            // than the last scan left it, in the direction it already points.
            const int refinement = bits.Bit();
            if (refinement < 0) {
                return false;
            }
            if (refinement != 0) {
                coefficients[0] += coefficients[0] >= 0 ? (1 << scan.successiveLow) : -(1 << scan.successiveLow);
            }
        }
        // A scan that covers only the first coefficient ends the block here, which is the whole of
        // what a first scan of a progressive file writes.
        if (scan.spectralEnd == 0) {
            return true;
        }
    }
    const int acTable = component.acTable;
    const int start = scan.spectralStart == 0 ? 1 : scan.spectralStart;
    if (acTable > 3 || !frame.acReady[acTable]) {
        return false;
    }
    if (scan.successiveHigh == 0) {
        if (eobRun > 0) {
            --eobRun;
            return true;
        }
        int k = start;
        while (k <= scan.spectralEnd) {
            const int symbol = frame.acTables[acTable].Decode(bits);
            if (symbol < 0) {
                return false;
            }
            const int zeros = symbol >> 4;
            const int size = symbol & 15;
            if (size == 0) {
                if (zeros < 15) {
                    eobRun = (1 << zeros) - 1;
                    if (zeros > 0) {
                        const int extra = bits.Bits(zeros);
                        if (extra < 0) {
                            return false;
                        }
                        eobRun += extra;
                    }
                    break;
                }
                // A run of sixteen noughts and no value: the band carries on after them.
                k += 16;
            } else {
                k += zeros;
                if (k > scan.spectralEnd) {
                    return false;
                }
                const int magnitude = bits.Bits(size);
                if (magnitude < 0) {
                    return false;
                }
                coefficients[kZigZag[k]] = Extend(magnitude, size) * (1 << scan.successiveLow);
                ++k;
            }
        }
        return true;
    }
    const int step = 1 << scan.successiveLow;
    int k = start;
    if (eobRun > 0) {
        // A block inside a run adds nothing to the band: it sharpens what an earlier pass left and
        // the run is one block shorter for it. The run is not the one this block starts, which is
        // why nothing here writes a coefficient of its own.
        --eobRun;
        while (k <= scan.spectralEnd) {
            int* coefficient = coefficients + kZigZag[k];
            ++k;
            if (*coefficient != 0) {
                const int refinement = bits.Bit();
                if (refinement < 0) {
                    return false;
                }
                if (refinement != 0 && (*coefficient & step) == 0) {
                    *coefficient += *coefficient > 0 ? step : -step;
                }
            }
        }
        return true;
    }
    while (k <= scan.spectralEnd) {
        const int symbol = frame.acTables[acTable].Decode(bits);
        if (symbol < 0) {
            return false;
        }
        int zeros = symbol >> 4;
        const int size = symbol & 15;
        int value = 0;
        if (size == 0) {
            if (zeros < 15) {
                eobRun = (1 << zeros) - 1;
                if (zeros > 0) {
                    const int extra = bits.Bits(zeros);
                    if (extra < 0) {
                        return false;
                    }
                    eobRun += extra;
                }
                // The rest of this block is refinement only, and so are the blocks the run covers
                // after it.
                zeros = 64;
            }
        } else {
            if (size != 1) {
                return false;
            }
            const int sign = bits.Bit();
            if (sign < 0) {
                return false;
            }
            value = sign != 0 ? step : -step;
        }
        while (k <= scan.spectralEnd) {
            int* coefficient = coefficients + kZigZag[k];
            ++k;
            if (*coefficient != 0) {
                const int refinement = bits.Bit();
                if (refinement < 0) {
                    return false;
                }
                if (refinement != 0 && (*coefficient & step) == 0) {
                    *coefficient += *coefficient > 0 ? step : -step;
                }
            } else if (zeros == 0) {
                *coefficient = value;
                // A block gains at most one coefficient in a refinement pass. The band is walked on
                // from after the one just written rather than ended, because the run that says the
                // rest of the band is finished may still be waiting to be read.
                break;
            } else {
                --zeros;
            }
        }
    }
    return true;
}

// Every block of one scan, in the order the file wrote them. One component on its own is written as
// that component's own grid of blocks read across and down; several components together are written
// as units that hold each component's share of them, which is what makes two components share one
// pass over the picture.
bool JpegDecodeScan(JpegBit& bits, JpegFrame& frame, const JpegScan& scan) {
    int slot[4] = {-1, -1, -1, -1};
    for (int index = 0; index < scan.componentCount; ++index) {
        int found = -1;
        for (int candidate = 0; candidate < frame.componentCount; ++candidate) {
            if (frame.components[candidate].id == scan.components[index]) {
                found = candidate;
                break;
            }
        }
        if (found < 0) {
            return false;
        }
        slot[index] = found;
        frame.components[found].dcTable = scan.dcTable[index];
        frame.components[found].acTable = scan.acTable[index];
    }
    int eobRun = 0;
    int sinceRestart = 0;
    if (scan.componentCount == 1) {
        JpegComponent& component = frame.components[slot[0]];
        for (int blockY = 0; blockY < component.scanBlocksHigh; ++blockY) {
            for (int blockX = 0; blockX < component.scanBlocksWide; ++blockX) {
                if (frame.restartInterval > 0 && sinceRestart == frame.restartInterval) {
                    bits.Restart();
                    component.prediction = 0;
                    eobRun = 0;
                    sinceRestart = 0;
                }
                const int blockIndex = blockY * component.blocksWide + blockX;
                if (!JpegDecodeBlock(bits, frame, component, blockIndex, scan, eobRun)) {
                    return false;
                }
                ++sinceRestart;
            }
        }
        return true;
    }
    const int unitWidth = frame.maxHorizontal * 8;
    const int unitHeight = frame.maxVertical * 8;
    const int unitsWide = (frame.width + unitWidth - 1) / unitWidth;
    const int unitsHigh = (frame.height + unitHeight - 1) / unitHeight;
    for (int unitY = 0; unitY < unitsHigh; ++unitY) {
        for (int unitX = 0; unitX < unitsWide; ++unitX) {
            if (frame.restartInterval > 0 && sinceRestart == frame.restartInterval) {
                bits.Restart();
                for (int index = 0; index < frame.componentCount; ++index) {
                    frame.components[index].prediction = 0;
                }
                eobRun = 0;
                sinceRestart = 0;
            }
            for (int index = 0; index < scan.componentCount; ++index) {
                JpegComponent& component = frame.components[slot[index]];
                for (int v = 0; v < component.vertical; ++v) {
                    for (int h = 0; h < component.horizontal; ++h) {
                        const int blockX = unitX * component.horizontal + h;
                        const int blockY = unitY * component.vertical + v;
                        if (blockX >= component.blocksWide || blockY >= component.blocksHigh) {
                            continue;
                        }
                        const int blockIndex = blockY * component.blocksWide + blockX;
                        if (!JpegDecodeBlock(bits, frame, component, blockIndex, scan, eobRun)) {
                            return false;
                        }
                    }
                }
            }
            ++sinceRestart;
        }
    }
    return true;
}

// Reads one scan's header out of the marker body and then the blocks it carries.
bool JpegReadScan(const unsigned char* data, int size, int cursor, int length, JpegFrame& frame, bool& progressive,
                  bool& haveFrame) {
    (void)progressive;
    const unsigned char* body = data + cursor + 2;
    const int bodySize = length - 2;
    if (!haveFrame || bodySize < 4) {
        return false;
    }
    JpegScan scan;
    scan.componentCount = body[0];
    if (scan.componentCount < 1 || scan.componentCount > 4 || bodySize < 4 + scan.componentCount * 2) {
        return false;
    }
    for (int index = 0; index < scan.componentCount; ++index) {
        scan.components[index] = body[1 + index * 2];
        scan.dcTable[index] = body[2 + index * 2] >> 4;
        scan.acTable[index] = body[2 + index * 2] & 15;
    }
    scan.spectralStart = body[1 + scan.componentCount * 2];
    scan.spectralEnd = body[2 + scan.componentCount * 2];
    scan.successiveHigh = body[3 + scan.componentCount * 2] >> 4;
    scan.successiveLow = body[3 + scan.componentCount * 2] & 15;
    if (scan.spectralStart > scan.spectralEnd || scan.spectralEnd > 63) {
        return false;
    }
    const int begin = cursor + length;
    const int end = SkipEntropyData(data, size, begin);
    JpegBit bits;
    bits.data = data;
    bits.size = end;
    bits.index = begin;
    return JpegDecodeScan(bits, frame, scan);
}

bool DecodeJpegImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    JpegFrame frame;
    bool progressive = false;
    bool haveFrame = false;
    int cursor = 2;
    while (cursor + 4 <= size) {
        if (data[cursor] != 0xFF) {
            return false;
        }
        while (cursor < size && data[cursor] == 0xFF) {
            ++cursor;
        }
        if (cursor >= size) {
            break;
        }
        const int marker = data[cursor++];
        if (marker == 0xD9) {
            break;
        }
        // A standalone marker carries no length and nothing the runtime reads.
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (cursor + 2 > size) {
            return false;
        }
        const int length = ReadBe16(data + cursor);
        if (length < 2 || cursor + length > size) {
            return false;
        }
        const unsigned char* body = data + cursor + 2;
        const int bodySize = length - 2;
        if (marker == 0xDB) {
            int offset = 0;
            while (offset < bodySize) {
                const int info = body[offset++];
                const int index = info & 15;
                if (index > 3 || offset + 64 > bodySize) {
                    return false;
                }
                for (int value = 0; value < 64; ++value) {
                    frame.quant[index][kZigZag[value]] = body[offset + value];
                }
                frame.quantUsed[index] = true;
                offset += 64;
            }
        } else if (marker == 0xC4) {
            int offset = 0;
            while (offset < bodySize) {
                const int info = body[offset++];
                const int index = info & 15;
                const bool ac = (info & 0x10) != 0;
                if (index > 3 || offset + 16 > bodySize) {
                    return false;
                }
                int lengths[16];
                int count = 0;
                for (int lengthIndex = 0; lengthIndex < 16; ++lengthIndex) {
                    lengths[lengthIndex] = body[offset + lengthIndex];
                    count += lengths[lengthIndex];
                }
                offset += 16;
                if (offset + count > bodySize) {
                    return false;
                }
                int values[256];
                for (int value = 0; value < count; ++value) {
                    values[value] = body[offset + value];
                }
                offset += count;
                JpegHuffman& table = ac ? frame.acTables[index] : frame.dcTables[index];
                if (!table.Build(lengths, values, count)) {
                    return false;
                }
                if (ac) {
                    frame.acReady[index] = true;
                } else {
                    frame.dcReady[index] = true;
                }
            }
        } else if (marker == 0xC0 || marker == 0xC1 || marker == 0xC2) {
            // Three kinds of frame are read here. Two of them write every coefficient of a block in
            // one pass and differ only in how wide a coefficient may be; the third writes the
            // picture in as many passes as it likes, one band of the block at a time, each pass
            // either adding coefficients or sharpening the ones already there.
            if (bodySize < 8) {
                return false;
            }
            progressive = marker == 0xC2;
            frame.height = ReadBe16(body + 1);
            frame.width = ReadBe16(body + 3);
            frame.componentCount = body[5];
            if (frame.componentCount < 1 || frame.componentCount > 3 || bodySize < 6 + frame.componentCount * 3) {
                return false;
            }
            if (frame.width < 1 || frame.height < 1 || frame.width > kMaxEdge || frame.height > kMaxEdge) {
                return false;
            }
            for (int index = 0; index < frame.componentCount; ++index) {
                JpegComponent& component = frame.components[index];
                component.id = body[6 + index * 3];
                component.horizontal = body[7 + index * 3] >> 4;
                component.vertical = body[7 + index * 3] & 15;
                component.quantTable = body[8 + index * 3];
                if (component.horizontal < 1 || component.vertical < 1 || component.horizontal > 4 || component.vertical > 4) {
                    return false;
                }
                if (component.horizontal > frame.maxHorizontal) {
                    frame.maxHorizontal = component.horizontal;
                }
                if (component.vertical > frame.maxVertical) {
                    frame.maxVertical = component.vertical;
                }
            }
            const int unitWidth = frame.maxHorizontal * 8;
            const int unitHeight = frame.maxVertical * 8;
            const int unitsWide = (frame.width + unitWidth - 1) / unitWidth;
            const int unitsHigh = (frame.height + unitHeight - 1) / unitHeight;
            for (int index = 0; index < frame.componentCount; ++index) {
                JpegComponent& component = frame.components[index];
                component.blocksWide = unitsWide * component.horizontal;
                component.blocksHigh = unitsHigh * component.vertical;
                component.scanBlocksWide =
                    (frame.width * component.horizontal + unitWidth - 1) / unitWidth;
                component.scanBlocksHigh =
                    (frame.height * component.vertical + unitHeight - 1) / unitHeight;
                component.coefficients.assign(
                    static_cast<std::size_t>(component.blocksWide) * static_cast<std::size_t>(component.blocksHigh) * 64u, 0);
            }
            haveFrame = true;
        } else if (marker == 0xC3 || (marker >= 0xC5 && marker <= 0xC7) || (marker >= 0xC9 && marker <= 0xCB) ||
                   (marker >= 0xCD && marker <= 0xCF)) {
            // Lossless, arithmetic and differential frames are not read here.
            return false;
        } else if (marker == 0xDD && bodySize >= 2) {
            frame.restartInterval = ReadBe16(body);
        } else if (marker == 0xDA) {
            if (!JpegReadScan(data, size, cursor, length, frame, progressive, haveFrame)) {
                return false;
            }
            cursor = SkipEntropyData(data, size, cursor + length);
            continue;
        }
        cursor += length;
    }
    if (!haveFrame) {
        return false;
    }
    // Every scan has been read, so every block can be made into the pixels it stands for. The
    // coefficients are scaled by the table they were coded against, turned back into a block of
    // brightness, and dropped into the plane of the component they belong to.
    std::vector<std::vector<int>> planes(static_cast<std::size_t>(frame.componentCount));
    for (int index = 0; index < frame.componentCount; ++index) {
        JpegComponent& component = frame.components[index];
        if (component.quantTable > 3 || !frame.quantUsed[component.quantTable]) {
            return false;
        }
        const int planeWidth = component.blocksWide * 8;
        const std::size_t planeBytes =
            static_cast<std::size_t>(planeWidth) * static_cast<std::size_t>(component.blocksHigh * 8);
        std::vector<int>& plane = planes[static_cast<std::size_t>(index)];
        plane.assign(planeBytes, 0);
        std::vector<int> block(64);
        std::vector<int> spatial(64);
        for (int blockY = 0; blockY < component.blocksHigh; ++blockY) {
            for (int blockX = 0; blockX < component.blocksWide; ++blockX) {
                const int* coefficients =
                    component.coefficients.data() +
                    static_cast<std::size_t>(blockY * component.blocksWide + blockX) * 64u;
                for (int value = 0; value < 64; ++value) {
                    block[static_cast<std::size_t>(value)] =
                        coefficients[value] * frame.quant[component.quantTable][value];
                }
                InverseDct(block.data(), spatial.data());
                for (int y = 0; y < 8; ++y) {
                    for (int x = 0; x < 8; ++x) {
                        plane[static_cast<std::size_t>((blockY * 8 + y) * planeWidth + blockX * 8 + x)] =
                            spatial[static_cast<std::size_t>(y * 8 + x)] + 128;
                    }
                }
            }
        }
        // The coefficients have given up their picture and are not needed again.
        component.coefficients.clear();
        component.coefficients.shrink_to_fit();
    }
    const int width = frame.width;
    const int height = frame.height;
    ImageFrame decoded;
    decoded.width = width;
    decoded.height = height;
    decoded.bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 255);
    const int unitWidth = frame.maxHorizontal * 8;
    const int unitHeight = frame.maxVertical * 8;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto sample = [&](int component) {
                const JpegComponent& source = frame.components[component];
                const int planeWidth = source.blocksWide * 8;
                const int planeHeight = source.blocksHigh * 8;
                const int sampleX = x * source.horizontal * 8 / unitWidth;
                const int sampleY = y * source.vertical * 8 / unitHeight;
                const int clampedX = sampleX >= planeWidth ? planeWidth - 1 : sampleX;
                const int clampedY = sampleY >= planeHeight ? planeHeight - 1 : sampleY;
                return planes[static_cast<std::size_t>(component)]
                             [static_cast<std::size_t>(clampedY * planeWidth + clampedX)];
            };
            int red = 0;
            int green = 0;
            int blue = 0;
            if (frame.componentCount == 1) {
                red = green = blue = sample(0);
            } else {
                const int luma = sample(0);
                const int cb = sample(1);
                const int cr = sample(2);
                red = static_cast<int>(luma + 1.402 * (cr - 128));
                green = static_cast<int>(luma - 0.344136 * (cb - 128) - 0.714136 * (cr - 128));
                blue = static_cast<int>(luma + 1.772 * (cb - 128));
            }
            PutBgra(decoded.bgra, (y * width + x) * 4, ClampByte(blue), ClampByte(green), ClampByte(red), 255);
        }
    }
    image.format = "jpeg";
    image.frames.push_back(std::move(decoded));
    return true;
}

bool DecodeGifImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 13 || (std::memcmp(data, "GIF87a", 6) != 0 && std::memcmp(data, "GIF89a", 6) != 0)) {
        return false;
    }
    GifReader reader;
    reader.data = data;
    reader.size = size;
    reader.index = 6;
    const int width = reader.Byte() | (reader.Byte() << 8);
    const int height = reader.Byte() | (reader.Byte() << 8);
    const int packed = reader.Byte();
    const int background = reader.Byte();
    reader.Byte();
    if (width < 1 || height < 1 || width > kMaxEdge || height > kMaxEdge || packed < 0) {
        return false;
    }
    std::vector<unsigned char> globalPalette;
    if ((packed & 0x80) != 0) {
        if (!ReadGifPalette(reader, 1 << ((packed & 7) + 1), globalPalette)) {
            return false;
        }
    }
    std::vector<unsigned char> canvas(static_cast<std::size_t>(width * height * 4), 0);
    if (!globalPalette.empty() && background >= 0) {
        const int entry = background * 3;
        if (entry + 2 < static_cast<int>(globalPalette.size())) {
            for (int pixel = 0; pixel < width * height; ++pixel) {
                PutBgra(canvas, pixel * 4, globalPalette[static_cast<std::size_t>(entry + 2)], globalPalette[static_cast<std::size_t>(entry + 1)],
                        globalPalette[static_cast<std::size_t>(entry)], 255);
            }
        }
    }
    int delay = 0;
    int transparent = -1;
    int disposal = 0;
    while (reader.index < reader.size) {
        const int separator = reader.Byte();
        if (separator == 0x3B) {
            break;
        }
        if (separator == 0x21) {
            const int label = reader.Byte();
            if (label == 0xF9) {
                const int block = reader.Byte();
                if (block < 4) {
                    return false;
                }
                const int flags = reader.Byte();
                delay = (reader.Byte() | (reader.Byte() << 8)) * 10;
                transparent = reader.Byte();
                if ((flags & 1) == 0) {
                    transparent = -1;
                }
                disposal = (flags >> 2) & 7;
                reader.Byte();
            } else {
                while (true) {
                    const int block = reader.Byte();
                    if (block <= 0) {
                        break;
                    }
                    if (!reader.Skip(block)) {
                        return false;
                    }
                }
            }
            continue;
        }
        if (separator != 0x2C) {
            return false;
        }
        const int left = reader.Byte() | (reader.Byte() << 8);
        const int top = reader.Byte() | (reader.Byte() << 8);
        const int frameWidth = reader.Byte() | (reader.Byte() << 8);
        const int frameHeight = reader.Byte() | (reader.Byte() << 8);
        const int framePacked = reader.Byte();
        if (frameWidth < 1 || frameHeight < 1) {
            return false;
        }
        std::vector<unsigned char> palette = globalPalette;
        if ((framePacked & 0x80) != 0) {
            if (!ReadGifPalette(reader, 1 << ((framePacked & 7) + 1), palette)) {
                return false;
            }
        }
        const int minimum = reader.Byte();
        if (minimum < 2 || minimum > 8) {
            return false;
        }
        std::vector<unsigned char> saved = canvas;
        std::vector<unsigned char> indices;
        if (!DecodeGifLzw(reader, minimum, frameWidth * frameHeight, indices)) {
            return false;
        }
        const std::vector<int> rowOrder = GifRowOrder(frameHeight, (framePacked & 0x40) != 0);
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const int destX = left + x;
                const int destY = top + rowOrder[static_cast<std::size_t>(y)];
                if (destX < 0 || destY < 0 || destX >= width || destY >= height) {
                    continue;
                }
                const std::size_t written = static_cast<std::size_t>(y * frameWidth + x);
                if (written >= indices.size()) {
                    // The codes ran out before this pixel: what is under the frame is left as it was.
                    continue;
                }
                const int color = indices[written];
                if (color == transparent) {
                    continue;
                }
                const int entry = color * 3;
                if (entry + 2 >= static_cast<int>(palette.size())) {
                    continue;
                }
                PutBgra(canvas, (destY * width + destX) * 4, palette[static_cast<std::size_t>(entry + 2)],
                        palette[static_cast<std::size_t>(entry + 1)], palette[static_cast<std::size_t>(entry)], 255);
            }
        }
        ImageFrame frame;
        frame.width = width;
        frame.height = height;
        frame.delayMs = delay > 0 ? delay : 100;
        frame.bgra = canvas;
        image.frames.push_back(std::move(frame));
        if (disposal == 2) {
            for (int y = 0; y < frameHeight; ++y) {
                for (int x = 0; x < frameWidth; ++x) {
                    const int destX = left + x;
                    const int destY = top + y;
                    if (destX >= 0 && destY >= 0 && destX < width && destY < height) {
                        PutBgra(canvas, (destY * width + destX) * 4, 0, 0, 0, 0);
                    }
                }
            }
        } else if (disposal == 3) {
            canvas.swap(saved);
        }
        delay = 0;
        transparent = -1;
        disposal = 0;
    }
    if (image.frames.empty()) {
        return false;
    }
    image.format = "gif";
    return true;
}

}  // namespace Runtime
