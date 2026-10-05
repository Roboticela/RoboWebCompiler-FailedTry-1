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

bool DecodeGifLzw(GifReader& reader, int minimumCode, int pixelCount, std::vector<unsigned char>& indices) {
    int clear = 1 << minimumCode;
    int end = clear + 1;
    int codeSize = minimumCode + 1;
    int next = end + 1;
    std::vector<int> prefix(4096);
    std::vector<unsigned char> suffix(4096);
    std::vector<int> length(4096, 1);
    for (int code = 0; code < clear; ++code) {
        suffix[static_cast<std::size_t>(code)] = static_cast<unsigned char>(code);
        length[static_cast<std::size_t>(code)] = 1;
    }
    unsigned int buffer = 0;
    int bits = 0;
    auto take = [&](int count) {
        while (bits < count) {
            const int block = reader.Byte();
            if (block < 0) {
                return -1;
            }
            if (block == 0) {
                return -2;
            }
            for (int index = 0; index < block; ++index) {
                const int value = reader.Byte();
                if (value < 0) {
                    return -1;
                }
                buffer |= static_cast<unsigned int>(value) << bits;
                bits += 8;
            }
        }
        const int value = static_cast<int>(buffer & ((1u << count) - 1u));
        buffer >>= count;
        bits -= count;
        return value;
    };
    int previous = -1;
    indices.clear();
    while (static_cast<int>(indices.size()) < pixelCount) {
        const int code = take(codeSize);
        if (code < 0) {
            return code == -2 && static_cast<int>(indices.size()) == pixelCount;
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
        std::vector<unsigned char> sequence;
        if (code < next) {
            int current = code;
            while (current >= clear) {
                sequence.push_back(suffix[static_cast<std::size_t>(current)]);
                current = prefix[static_cast<std::size_t>(current)];
            }
            sequence.push_back(suffix[static_cast<std::size_t>(current)]);
        } else if (code == next && previous >= 0) {
            int current = previous;
            while (current >= clear) {
                sequence.push_back(suffix[static_cast<std::size_t>(current)]);
                current = prefix[static_cast<std::size_t>(current)];
            }
            sequence.push_back(suffix[static_cast<std::size_t>(current)]);
            sequence.insert(sequence.begin(), sequence.back());
        } else {
            return false;
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
            if (next == (1 << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }
        previous = code;
    }
    while (true) {
        const int block = reader.Byte();
        if (block <= 0) {
            break;
        }
        if (!reader.Skip(block)) {
            return false;
        }
    }
    return static_cast<int>(indices.size()) >= pixelCount;
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
    std::vector<unsigned char> idat;
    std::vector<unsigned char> palette;
    std::vector<unsigned char> transparency;
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
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            idat.insert(idat.end(), chunk, chunk + length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        cursor += 12 + length;
    }
    if (width < 1 || height < 1 || width > kMaxEdge || height > kMaxEdge || colorType < 0) {
        return false;
    }
    const int channels = colorType == 2 ? 3 : colorType == 6 ? 4 : colorType == 4 ? 2 : 1;
    std::vector<unsigned char> inflated;
    if (!InflateZlib(idat.data(), static_cast<int>(idat.size()), inflated)) {
        return false;
    }
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 0);
    if (interlace == 0) {
        std::vector<unsigned char> samples;
        if (!Unfilter(inflated, width, height, channels, bitDepth, samples) ||
            !ExpandPng(samples, width, height, colorType, bitDepth, palette, transparency, pixels)) {
            return false;
        }
    } else if (interlace == 1) {
        int offset = 0;
        for (int pass = 0; pass < 7; ++pass) {
            const int passWidth = (width - kAdam7ColStart[pass] + kAdam7ColStep[pass] - 1) / kAdam7ColStep[pass];
            const int passHeight = (height - kAdam7RowStart[pass] + kAdam7RowStep[pass] - 1) / kAdam7RowStep[pass];
            std::vector<unsigned char> samples;
            if (passWidth <= 0 || passHeight <= 0) {
                continue;
            }
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
    } else {
        return false;
    }
    ImageFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra = std::move(pixels);
    image.format = "png";
    image.frames.push_back(std::move(frame));
    return true;
}

bool DecodeJpegImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 4 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }
    int width = 0;
    int height = 0;
    int componentCount = 0;
    int componentId[3] = {};
    int horizontal[3] = {};
    int vertical[3] = {};
    int tableIndex[3] = {};
    int quant[4][64] = {};
    bool quantUsed[4] = {};
    JpegHuffman dcTables[4];
    JpegHuffman acTables[4];
    bool dcReady[4] = {};
    bool acReady[4] = {};
    int restart = 0;
    int cursor = 2;
    bool startOfScan = false;
    int scanComponents = 0;
    int scanId[3] = {};
    int scanDc[3] = {};
    int scanAc[3] = {};
    int spectralStart = 0;
    int spectralEnd = 63;
    while (cursor + 4 <= size && !startOfScan) {
        if (data[cursor] != 0xFF) {
            return false;
        }
        while (cursor < size && data[cursor] == 0xFF) {
            ++cursor;
        }
        if (cursor >= size) {
            return false;
        }
        const int marker = data[cursor++];
        if (marker == 0xD9) {
            return false;
        }
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
                    quant[index][kZigZag[value]] = body[offset + value];
                }
                quantUsed[index] = true;
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
                JpegHuffman& table = ac ? acTables[index] : dcTables[index];
                if (!table.Build(lengths, values, count)) {
                    return false;
                }
                if (ac) {
                    acReady[index] = true;
                } else {
                    dcReady[index] = true;
                }
            }
        } else if (marker == 0xC0) {
            if (bodySize < 8) {
                return false;
            }
            height = ReadBe16(body + 1);
            width = ReadBe16(body + 3);
            componentCount = body[5];
            if (componentCount < 1 || componentCount > 3 || bodySize < 6 + componentCount * 3) {
                return false;
            }
            for (int index = 0; index < componentCount; ++index) {
                componentId[index] = body[6 + index * 3];
                horizontal[index] = body[7 + index * 3] >> 4;
                vertical[index] = body[7 + index * 3] & 15;
                tableIndex[index] = body[8 + index * 3];
            }
        } else if (marker == 0xC2 || marker == 0xC1) {
            return false;
        } else if (marker == 0xDD && bodySize >= 2) {
            restart = ReadBe16(body);
        } else if (marker == 0xDA) {
            if (bodySize < 3) {
                return false;
            }
            scanComponents = body[0];
            if (scanComponents < 1 || scanComponents > 3 || bodySize < 4 + scanComponents * 2) {
                return false;
            }
            for (int index = 0; index < scanComponents; ++index) {
                scanId[index] = body[1 + index * 2];
                scanDc[index] = body[2 + index * 2] >> 4;
                scanAc[index] = body[2 + index * 2] & 15;
            }
            spectralStart = body[1 + scanComponents * 2];
            spectralEnd = body[2 + scanComponents * 2];
            startOfScan = true;
            cursor += length;
            break;
        }
        if (!startOfScan) {
            cursor += length;
        }
    }
    if (!startOfScan || width < 1 || height < 1 || width > kMaxEdge || height > kMaxEdge || spectralStart != 0 || spectralEnd != 63) {
        return false;
    }
    int maxH = 1;
    int maxV = 1;
    for (int index = 0; index < componentCount; ++index) {
        if (horizontal[index] > maxH) {
            maxH = horizontal[index];
        }
        if (vertical[index] > maxV) {
            maxV = vertical[index];
        }
    }
    const int mcuWidth = maxH * 8;
    const int mcuHeight = maxV * 8;
    const int mcuColumns = (width + mcuWidth - 1) / mcuWidth;
    const int mcuRows = (height + mcuHeight - 1) / mcuHeight;
    std::vector<std::vector<int>> planes(static_cast<std::size_t>(componentCount));
    for (int index = 0; index < componentCount; ++index) {
        const int planeWidth = mcuColumns * horizontal[index] * 8;
        const int planeHeight = mcuRows * vertical[index] * 8;
        planes[static_cast<std::size_t>(index)].assign(static_cast<std::size_t>(planeWidth * planeHeight), 0);
    }
    JpegBit bits;
    bits.data = data + cursor;
    bits.size = size - cursor;
    int prediction[3] = {};
    int mcuSinceRestart = 0;
    auto decodeBlock = [&](int component, int block[64]) {
        const int dcTable = scanDc[component];
        const int acTable = scanAc[component];
        const int quantId = tableIndex[component];
        if (dcTable > 3 || acTable > 3 || !dcReady[dcTable] || !acReady[acTable] || !quantUsed[quantId]) {
            return false;
        }
        const int dcSymbol = dcTables[dcTable].Decode(bits);
        if (dcSymbol < 0) {
            return false;
        }
        const int dcMagnitude = dcSymbol == 0 ? 0 : bits.Bits(dcSymbol);
        if (dcMagnitude < 0 && dcSymbol != 0) {
            return false;
        }
        prediction[component] += Extend(dcMagnitude, dcSymbol);
        block[0] = prediction[component] * quant[quantId][0];
        for (int index = 1; index < 64;) {
            const int symbol = acTables[acTable].Decode(bits);
            if (symbol < 0) {
                return false;
            }
            if (symbol == 0) {
                break;
            }
            const int zeros = symbol >> 4;
            const int magnitudeBits = symbol & 15;
            index += zeros;
            if (index >= 64) {
                return magnitudeBits == 0;
            }
            const int magnitude = magnitudeBits == 0 ? 0 : bits.Bits(magnitudeBits);
            if (magnitude < 0) {
                return false;
            }
            block[kZigZag[index]] = Extend(magnitude, magnitudeBits) * quant[quantId][kZigZag[index]];
            ++index;
        }
        return true;
    };
    for (int row = 0; row < mcuRows; ++row) {
        for (int column = 0; column < mcuColumns; ++column) {
            if (restart > 0 && mcuSinceRestart == restart) {
                bits.bits = 0;
                while (bits.index < bits.size && bits.data[bits.index] == 0xFF) {
                    ++bits.index;
                    if (bits.index < bits.size && bits.data[bits.index] >= 0xD0 && bits.data[bits.index] <= 0xD7) {
                        ++bits.index;
                        break;
                    }
                }
                prediction[0] = prediction[1] = prediction[2] = 0;
                mcuSinceRestart = 0;
            }
            for (int component = 0; component < componentCount; ++component) {
                const int planeWidth = mcuColumns * horizontal[component] * 8;
                for (int blockY = 0; blockY < vertical[component]; ++blockY) {
                    for (int blockX = 0; blockX < horizontal[component]; ++blockX) {
                        int block[64] = {};
                        if (!decodeBlock(component, block)) {
                            return false;
                        }
                        int spatial[64];
                        InverseDct(block, spatial);
                        const int originX = column * horizontal[component] * 8 + blockX * 8;
                        const int originY = row * vertical[component] * 8 + blockY * 8;
                        for (int y = 0; y < 8; ++y) {
                            for (int x = 0; x < 8; ++x) {
                                planes[static_cast<std::size_t>(component)][static_cast<std::size_t>((originY + y) * planeWidth + originX + x)] =
                                    spatial[y * 8 + x] + 128;
                            }
                        }
                    }
                }
            }
            ++mcuSinceRestart;
        }
    }
    ImageFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra.assign(static_cast<std::size_t>(width * height * 4), 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            auto sample = [&](int component) {
                const int planeWidth = mcuColumns * horizontal[component] * 8;
                const int planeHeight = mcuRows * vertical[component] * 8;
                const int sampleX = x * horizontal[component] * 8 / mcuWidth;
                const int sampleY = y * vertical[component] * 8 / mcuHeight;
                const int clampedX = sampleX >= planeWidth ? planeWidth - 1 : sampleX;
                const int clampedY = sampleY >= planeHeight ? planeHeight - 1 : sampleY;
                return planes[static_cast<std::size_t>(component)][static_cast<std::size_t>(clampedY * planeWidth + clampedX)];
            };
            int red = 0;
            int green = 0;
            int blue = 0;
            if (componentCount == 1) {
                red = green = blue = sample(0);
            } else {
                const int luma = sample(0);
                const int cb = sample(1);
                const int cr = sample(2);
                red = static_cast<int>(luma + 1.402 * (cr - 128));
                green = static_cast<int>(luma - 0.344136 * (cb - 128) - 0.714136 * (cr - 128));
                blue = static_cast<int>(luma + 1.772 * (cb - 128));
            }
            PutBgra(frame.bgra, (y * width + x) * 4, ClampByte(blue), ClampByte(green), ClampByte(red), 255);
        }
    }
    image.format = "jpeg";
    image.frames.push_back(std::move(frame));
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
        for (int y = 0; y < frameHeight; ++y) {
            for (int x = 0; x < frameWidth; ++x) {
                const int destX = left + x;
                const int destY = top + y;
                if (destX < 0 || destY < 0 || destX >= width || destY >= height) {
                    continue;
                }
                const int color = indices[static_cast<std::size_t>(y * frameWidth + x)];
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
