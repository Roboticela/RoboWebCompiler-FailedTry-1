#include "rendering/image/Vp8LImage.h"

#include <cstdint>
#include <vector>

namespace Runtime {
namespace {

const int kMaxEdge = 8192;
const int kNumLiteralCodes = 256;
const int kNumLengthCodes = 24;
const int kNumDistanceCodes = 40;
const int kNumCodeLengthCodes = 19;
const int kMaxCacheBits = 11;
const int kNumHuffmanCodes = 5;

// The pixel a predictor starts from where there is nothing above or to the left of it. It is a
// solid black rather than a zero, because a zero would carry alpha 0 and every border pixel of a
// picture would come back transparent.
const std::uint32_t kPredictorBlack = 0xff000000u;

// The order the code lengths of the code-length alphabet are written in, which is not the order of
// the symbols: the two repeat codes and the code of length one come first.
const int kCodeLengthCodeOrder[kNumCodeLengthCodes] = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// The distance a distance code stands for, as offsets from the pixel being read. The first hundred
// and twenty codes name neighbours a few rows up and a few columns either side, which is a far
// cheaper way to write "the pixel above" than the number of pixels back that it really is. Every
// code above them names the distance itself, less the hundred and twenty used here.
const int kDistanceOffsets[240] = {
    0, 1, 1, 0, 1, 1, -1, 1, 0, 2, 2, 0, 1, 2, -1, 2, 2, 1, -2, 1, 2, 2, -2, 2, 0, 3, 3, 0, 1, 3, -1, 3, 3, 1, -3, 1, 2, 3, -2, 3, 3, 2, -3, 2, 0, 4, 4, 0,
    1, 4, -1, 4, 4, 1, -4, 1, 3, 3, -3, 3, 2, 4, -2, 4, 4, 2, -4, 2, 0, 5, 3, 4, -3, 4, 4, 3, -4, 3, 5, 0, 1, 5, -1, 5, 5, 1, -5, 1, 2, 5, -2, 5, 5, 2, -5, 2, 4, 4,
    -4, 4, 3, 5, -3, 5, 5, 3, -5, 3, 0, 6, 6, 0, 1, 6, -1, 6, 6, 1, -6, 1, 2, 6, -2, 6, 6, 2, -6, 2, 4, 5, -4, 5, 5, 4, -5, 4, 3, 6, -3, 6, 6, 3, -6, 3, 0, 7,
    7, 0, 1, 7, -1, 7, 5, 5, -5, 5, 7, 1, -7, 1, 4, 6, -4, 6, 6, 4, -6, 4, 2, 7, -2, 7, 7, 2, -7, 2, 3, 7, -3, 7, 7, 3, -7, 3, 5, 6, -5, 6, 6, 5, -6, 5, 8, 0,
    4, 7, -4, 7, 7, 4, -7, 4, 8, 1, 8, 2, 6, 6, -6, 6, 8, 3, 5, 7, -5, 7, 7, 5, -7, 5, 8, 4, 6, 7, -6, 7, 7, 6, -7, 6, 8, 5, 7, 7, -7, 7, 8, 6, 8, 7};

int SubSampleSize(int size, int bits) {
    return (size + (1 << bits) - 1) >> bits;
}

int Clip255(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 255) {
        return 255;
    }
    return value;
}

// The bits of a picture, least significant bit of each byte first, which is the order every value
// of the format is written in. Bytes are drawn into a register as the values need them and never
// all at once, so a value that straddles two bytes costs nothing extra and a stream that stops
// short is noticed rather than read past.
struct BitReader {
    const unsigned char* data = nullptr;
    int size = 0;
    int index = 0;
    std::uint64_t buffer = 0;
    int available = 0;
    bool exhausted = false;

    void Open(const unsigned char* bytes, int length) {
        data = bytes;
        size = length;
        index = 0;
        buffer = 0;
        available = 0;
        exhausted = false;
    }

    void Fill(int needed) {
        while (available < needed && index < size) {
            buffer |= static_cast<std::uint64_t>(data[index++]) << available;
            available += 8;
        }
        if (available < needed) {
            exhausted = true;
        }
    }

    int Read(int bits) {
        if (bits <= 0) {
            return 0;
        }
        if (available < bits) {
            Fill(bits);
        }
        if (available < bits) {
            exhausted = true;
            return 0;
        }
        const std::uint64_t mask = (bits >= 64) ? ~static_cast<std::uint64_t>(0) : ((static_cast<std::uint64_t>(1) << bits) - 1);
        const int value = static_cast<int>(buffer & mask);
        buffer >>= bits;
        available -= bits;
        return value;
    }
};

// One prefix code of the format. Its code lengths are canonical, so the codes of a given length are
// laid out in the order of the symbols they stand for and no table of codes has to be written down:
// walking the lengths from one upwards and counting how many of each have been passed finds the
// symbol a run of bits stands for. A code holding a single symbol is the one case where the bits
// run out before any could be read, and it is marked so it can be answered without reading.
struct HuffmanTree {
    int counts[16] = {};
    std::vector<int> symbols;
    int singleSymbol = -1;
    int maxLength = 0;

    bool Build(const std::vector<int>& lengths) {
        for (int length = 0; length < 16; ++length) {
            counts[length] = 0;
        }
        symbols.clear();
        singleSymbol = -1;
        maxLength = 0;
        int total = 0;
        for (std::size_t index = 0; index < lengths.size(); ++index) {
            const int length = lengths[index];
            if (length < 0 || length > 15) {
                return false;
            }
            if (length > 0) {
                ++counts[length];
                ++total;
            }
        }
        if (total == 0) {
            return false;
        }
        if (total == 1) {
            for (std::size_t index = 0; index < lengths.size(); ++index) {
                if (lengths[index] > 0) {
                    singleSymbol = static_cast<int>(index);
                    break;
                }
            }
            return true;
        }
        int offsets[17] = {};
        for (int length = 1; length < 16; ++length) {
            offsets[length + 1] = offsets[length] + counts[length];
        }
        symbols.resize(static_cast<std::size_t>(total));
        for (std::size_t index = 0; index < lengths.size(); ++index) {
            const int length = lengths[index];
            if (length > 0) {
                symbols[static_cast<std::size_t>(offsets[length]++)] = static_cast<int>(index);
            }
        }
        for (int length = 15; length >= 1; --length) {
            if (counts[length] > 0) {
                maxLength = length;
                break;
            }
        }
        return true;
    }

    int Decode(BitReader& reader) const {
        if (singleSymbol >= 0) {
            return singleSymbol;
        }
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= maxLength; ++length) {
            code |= reader.Read(1);
            const int count = counts[length];
            if (code - first < count) {
                const int position = index + (code - first);
                if (position < 0 || position >= static_cast<int>(symbols.size())) {
                    return -1;
                }
                return symbols[static_cast<std::size_t>(position)];
            }
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }
};

struct HuffmanGroup {
    HuffmanTree trees[kNumHuffmanCodes];
};

struct Transform {
    int type = 0;
    int bits = 0;
    int xsize = 0;
    int ysize = 0;
    std::vector<std::uint32_t> data;
};

// One pixel of each channel added to the pixel it was predicted from, without carrying: a channel
// that overflows wraps within its own byte, which is what makes the sum of a predictor and its
// residue an exact inverse of the subtraction that produced the residue.
std::uint32_t AddPixels(std::uint32_t left, std::uint32_t right) {
    const std::uint32_t alphaAndGreen = (left & 0xff00ff00u) + (right & 0xff00ff00u);
    const std::uint32_t redAndBlue = (left & 0x00ff00ffu) + (right & 0x00ff00ffu);
    return (alphaAndGreen & 0xff00ff00u) | (redAndBlue & 0x00ff00ffu);
}

std::uint32_t Average2(std::uint32_t first, std::uint32_t second) {
    return (((first ^ second) & 0xfefefefeu) >> 1) + (first & second);
}

int AbsoluteChannelDifference(int first, int second) {
    const int difference = first - second;
    return difference < 0 ? -difference : difference;
}

// Which of the left and top pixels is the better guess for this one: the one whose distance from
// the top-left pixel is smaller measures how well it agrees with it across all four channels.
std::uint32_t Select(std::uint32_t left, std::uint32_t top, std::uint32_t topLeft) {
    int leftDistance = 0;
    int topDistance = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int l = static_cast<int>((left >> shift) & 0xff);
        const int t = static_cast<int>((top >> shift) & 0xff);
        const int c = static_cast<int>((topLeft >> shift) & 0xff);
        leftDistance += AbsoluteChannelDifference(l, c);
        topDistance += AbsoluteChannelDifference(t, c);
    }
    return (topDistance >= leftDistance) ? top : left;
}

std::uint32_t ClampAddSubtractFull(std::uint32_t first, std::uint32_t second, std::uint32_t third) {
    std::uint32_t result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int value = static_cast<int>((first >> shift) & 0xff) + static_cast<int>((second >> shift) & 0xff) - static_cast<int>((third >> shift) & 0xff);
        result |= static_cast<std::uint32_t>(Clip255(value)) << shift;
    }
    return result;
}

std::uint32_t ClampAddSubtractHalf(std::uint32_t first, std::uint32_t second, std::uint32_t third) {
    const std::uint32_t average = Average2(first, second);
    std::uint32_t result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const int value = static_cast<int>((average >> shift) & 0xff);
        const int other = static_cast<int>((third >> shift) & 0xff);
        result |= static_cast<std::uint32_t>(Clip255(value + (value - other) / 2)) << shift;
    }
    return result;
}

std::uint32_t Predict(int mode, std::uint32_t left, std::uint32_t top, std::uint32_t topLeft, std::uint32_t topRight) {
    switch (mode) {
        case 0:
            return kPredictorBlack;
        case 1:
            return left;
        case 2:
            return top;
        case 3:
            return topRight;
        case 4:
            return topLeft;
        case 5:
            return Average2(Average2(left, topRight), top);
        case 6:
            return Average2(left, topLeft);
        case 7:
            return Average2(left, top);
        case 8:
            return Average2(topLeft, top);
        case 9:
            return Average2(top, topRight);
        case 10:
            return Average2(Average2(left, topLeft), Average2(top, topRight));
        case 11:
            return Select(left, top, topLeft);
        case 12:
            return ClampAddSubtractFull(left, top, topLeft);
        case 13:
            return ClampAddSubtractHalf(left, top, topLeft);
        default:
            return kPredictorBlack;
    }
}

// The color transform measures how much of the red and blue of a pixel can be guessed from its
// green, and how much of its blue can be guessed from its red. The guess is written in the three
// color channels of a pixel of the transform image, a signed 3.5-bit number in each, and adding the
// guesses back is the whole of the inverse.
int ColorTransformDelta(int predictor, int channel) {
    const int signedPredictor = (predictor & 0x80) ? predictor - 0x100 : predictor;
    const int signedChannel = (channel & 0x80) ? channel - 0x100 : channel;
    return (signedPredictor * signedChannel) >> 5;
}

std::uint32_t ColorTransformInverse(std::uint32_t colorCode, std::uint32_t argb) {
    const int greenToRed = static_cast<int>(colorCode & 0xff);
    const int greenToBlue = static_cast<int>((colorCode >> 8) & 0xff);
    const int redToBlue = static_cast<int>((colorCode >> 16) & 0xff);
    const int green = static_cast<int>((argb >> 8) & 0xff);
    int red = static_cast<int>((argb >> 16) & 0xff);
    int blue = static_cast<int>(argb & 0xff);
    red = (red + ColorTransformDelta(greenToRed, green)) & 0xff;
    blue = (blue + ColorTransformDelta(greenToBlue, green)) & 0xff;
    blue = (blue + ColorTransformDelta(redToBlue, red)) & 0xff;
    return (argb & 0xff00ff00u) | (static_cast<std::uint32_t>(red) << 16) | static_cast<std::uint32_t>(blue);
}

// The format in one place. Everything below reads from one bit reader, so the subimages of the
// transforms are read out of the middle of the picture's own stream and continue where they end.
class Vp8LReader {
public:
    bool Read(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra);

private:
    bool ReadStream(int xsize, int ysize, bool isLevel0, std::vector<std::uint32_t>& pixels);
    bool ReadTransform(int& xsize, int& ysize);
    bool ReadHuffmanCodes(int xsize, int ysize, int colorCacheBits, bool allowGroups, std::vector<HuffmanGroup>& groups,
                          std::vector<int>& groupMap, int& groupBits);
    bool ReadHuffmanCode(int alphabetSize, HuffmanTree& tree);
    bool ReadCodeLengths(const int* codeLengthCodeLengths, int numSymbols, std::vector<int>& lengths);
    bool ReadPixels(int xsize, int ysize, int colorCacheBits, const std::vector<HuffmanGroup>& groups,
                    const std::vector<int>& groupMap, int groupBits, std::vector<std::uint32_t>& pixels);

    BitReader reader;
    std::vector<Transform> transforms;
};

int ReadPrefixValue(BitReader& reader, int prefixCode) {
    if (prefixCode < 4) {
        return prefixCode + 1;
    }
    const int extraBits = (prefixCode - 2) >> 1;
    const int offset = (2 + (prefixCode & 1)) << extraBits;
    return offset + reader.Read(extraBits) + 1;
}

int PlaneDistanceToScanLine(int xsize, int distanceCode) {
    if (distanceCode > 120) {
        return distanceCode - 120;
    }
    const int xi = kDistanceOffsets[(distanceCode - 1) * 2];
    const int yi = kDistanceOffsets[(distanceCode - 1) * 2 + 1];
    const int distance = xi + yi * xsize;
    return distance >= 1 ? distance : 1;
}

bool Vp8LReader::ReadCodeLengths(const int* codeLengthCodeLengths, int numSymbols, std::vector<int>& lengths) {
    std::vector<int> codeLengths(kNumCodeLengthCodes);
    for (int index = 0; index < kNumCodeLengthCodes; ++index) {
        codeLengths[static_cast<std::size_t>(index)] = codeLengthCodeLengths[index];
    }
    HuffmanTree tree;
    if (!tree.Build(codeLengths)) {
        return false;
    }
    int maxSymbol = numSymbols;
    if (reader.Read(1)) {
        const int lengthBits = 2 + 2 * reader.Read(3);
        maxSymbol = 2 + reader.Read(lengthBits);
        if (maxSymbol > numSymbols) {
            return false;
        }
    }
    const int extraBits[3] = {2, 3, 7};
    const int repeatOffsets[3] = {3, 3, 11};
    int symbol = 0;
    int previousLength = 8;
    while (symbol < numSymbols) {
        if (maxSymbol-- == 0) {
            break;
        }
        const int codeLength = tree.Decode(reader);
        if (codeLength < 0) {
            return false;
        }
        if (codeLength < 16) {
            lengths[static_cast<std::size_t>(symbol++)] = codeLength;
            if (codeLength != 0) {
                previousLength = codeLength;
            }
            continue;
        }
        const int slot = codeLength - 16;
        if (slot > 2) {
            return false;
        }
        const int repeat = reader.Read(extraBits[slot]) + repeatOffsets[slot];
        if (symbol + repeat > numSymbols) {
            return false;
        }
        const int fill = codeLength == 16 ? previousLength : 0;
        for (int index = 0; index < repeat; ++index) {
            lengths[static_cast<std::size_t>(symbol++)] = fill;
        }
    }
    return !reader.exhausted;
}

bool Vp8LReader::ReadHuffmanCode(int alphabetSize, HuffmanTree& tree) {

    std::vector<int> lengths(static_cast<std::size_t>(alphabetSize), 0);
    if (reader.Read(1)) {
        // One or two symbols of a single bit each, written down plainly rather than as lengths. It
        // is how nearly every real prefix code of the format is written, because most pictures have
        // only one or two colors in a block.
        const int numSymbols = reader.Read(1) + 1;
        const int firstIsEightBits = reader.Read(1);
        int symbol = reader.Read(firstIsEightBits ? 8 : 1);
        if (symbol >= alphabetSize) {
            return false;
        }
        lengths[static_cast<std::size_t>(symbol)] = 1;
        if (numSymbols == 2) {
            symbol = reader.Read(8);
            if (symbol >= alphabetSize) {
                return false;
            }
            lengths[static_cast<std::size_t>(symbol)] = 1;
        }
    } else {
        int codeLengthCodeLengths[kNumCodeLengthCodes] = {};
        const int numCodes = reader.Read(4) + 4;
        if (numCodes > kNumCodeLengthCodes) {
            return false;
        }
        for (int index = 0; index < numCodes; ++index) {
            codeLengthCodeLengths[kCodeLengthCodeOrder[index]] = reader.Read(3);
        }
        if (!ReadCodeLengths(codeLengthCodeLengths, alphabetSize, lengths)) {
            return false;
        }
    }
    if (reader.exhausted) {
        return false;
    }
    return tree.Build(lengths);
}

bool Vp8LReader::ReadHuffmanCodes(int xsize, int ysize, int colorCacheBits, bool allowGroups, std::vector<HuffmanGroup>& groups,
                                 std::vector<int>& groupMap, int& groupBits) {
    groups.clear();
    groupMap.clear();
    groupBits = 0;
    int numGroups = 1;
    if (allowGroups && reader.Read(1)) {
        // A second, smaller picture saying which prefix code group each block of the real picture
        // uses. It is read by the same reader as everything else, and it is read before the groups
        // themselves because it is what says how many of them there are.
        groupBits = reader.Read(3) + 2;
        const int entropyWidth = SubSampleSize(xsize, groupBits);
        const int entropyHeight = SubSampleSize(ysize, groupBits);
        std::vector<std::uint32_t> entropy;
        if (!ReadStream(entropyWidth, entropyHeight, false, entropy)) {
            return false;
        }
        groupMap.resize(entropy.size());
        for (std::size_t index = 0; index < entropy.size(); ++index) {
            const int group = static_cast<int>((entropy[index] >> 8) & 0xffff);
            groupMap[index] = group;
            if (group + 1 > numGroups) {
                numGroups = group + 1;
            }
        }
    }
    const int alphabetSizes[kNumHuffmanCodes] = {kNumLiteralCodes + kNumLengthCodes + (colorCacheBits > 0 ? (1 << colorCacheBits) : 0), kNumLiteralCodes,
                                                 kNumLiteralCodes, kNumLiteralCodes, kNumDistanceCodes};
    groups.resize(static_cast<std::size_t>(numGroups));
    for (int group = 0; group < numGroups; ++group) {
        for (int tree = 0; tree < kNumHuffmanCodes; ++tree) {
            if (!ReadHuffmanCode(alphabetSizes[tree], groups[static_cast<std::size_t>(group)].trees[tree])) {
                return false;
            }
        }
    }
    return true;
}

bool Vp8LReader::ReadPixels(int xsize, int ysize, int colorCacheBits, const std::vector<HuffmanGroup>& groups,
                            const std::vector<int>& groupMap, int groupBits, std::vector<std::uint32_t>& pixels) {
    const int colorCacheSize = colorCacheBits > 0 ? (1 << colorCacheBits) : 0;
    const int lengthLimit = kNumLiteralCodes + kNumLengthCodes;
    const int colorLimit = lengthLimit + colorCacheSize;
    std::vector<std::uint32_t> colorCache(static_cast<std::size_t>(colorCacheSize > 0 ? colorCacheSize : 1), 0);
    const int cacheShift = 32 - colorCacheBits;
    const int groupWidth = SubSampleSize(xsize, groupBits);
    pixels.assign(static_cast<std::size_t>(xsize) * static_cast<std::size_t>(ysize), 0);
    const HuffmanGroup* group = &groups.front();
    int x = 0;
    int y = 0;
    std::size_t written = 0;
    while (y < ysize) {
        if (!groupMap.empty()) {
            const int position = (y >> groupBits) * groupWidth + (x >> groupBits);
            if (position < 0 || position >= static_cast<int>(groupMap.size())) {
                return false;
            }
            const int index = groupMap[static_cast<std::size_t>(position)];
            if (index < 0 || index >= static_cast<int>(groups.size())) {
                return false;
            }
            group = &groups[static_cast<std::size_t>(index)];
        }
        const int symbol = group->trees[0].Decode(reader);
        if (symbol < 0) {
            return false;
        }
        if (symbol < kNumLiteralCodes) {
            const int red = group->trees[1].Decode(reader);
            const int blue = group->trees[2].Decode(reader);
            const int alpha = group->trees[3].Decode(reader);
            if (red < 0 || blue < 0 || alpha < 0) {
                return false;
            }
            const std::uint32_t pixel = (static_cast<std::uint32_t>(alpha) << 24) | (static_cast<std::uint32_t>(red) << 16) |
                                        (static_cast<std::uint32_t>(symbol) << 8) | static_cast<std::uint32_t>(blue);
            pixels[written] = pixel;
            if (colorCacheSize > 0) {
                colorCache[static_cast<std::size_t>((pixel * 0x1e35a7bdu) >> cacheShift)] = pixel;
            }
            ++x;
            ++written;
        } else if (symbol < lengthLimit) {
            const int length = ReadPrefixValue(reader, symbol - kNumLiteralCodes);
            const int distanceSymbol = group->trees[4].Decode(reader);
            if (distanceSymbol < 0 || distanceSymbol >= kNumDistanceCodes) {
                return false;
            }
            const int distanceCode = ReadPrefixValue(reader, distanceSymbol);
            const int distance = PlaneDistanceToScanLine(xsize, distanceCode);
            if (distance <= 0 || static_cast<long long>(written) < distance) {
                return false;
            }
            for (int index = 0; index < length; ++index) {
                if (y >= ysize || written >= pixels.size()) {
                    break;
                }
                const std::uint32_t pixel = pixels[written - static_cast<std::size_t>(distance)];
                pixels[written] = pixel;
                if (colorCacheSize > 0) {
                    colorCache[static_cast<std::size_t>((pixel * 0x1e35a7bdu) >> cacheShift)] = pixel;
                }
                ++written;
                if (++x >= xsize) {
                    x = 0;
                    ++y;
                }
            }
            continue;
        } else if (symbol < colorLimit) {
            const int key = symbol - lengthLimit;
            if (key >= colorCacheSize) {
                return false;
            }
            const std::uint32_t pixel = colorCache[static_cast<std::size_t>(key)];
            pixels[written] = pixel;
            colorCache[static_cast<std::size_t>((pixel * 0x1e35a7bdu) >> cacheShift)] = pixel;
            ++x;
            ++written;
        } else {
            return false;
        }
        if (x >= xsize) {
            x = 0;
            ++y;
        }
    }
    return true;
}

bool Vp8LReader::ReadTransform(int& xsize, int& ysize) {
    Transform transform;
    transform.type = reader.Read(2);
    transform.xsize = xsize;
    transform.ysize = ysize;
    transforms.push_back(std::move(transform));
    Transform& stored = transforms.back();
    switch (stored.type) {
        case 0:
        case 1: {
            stored.bits = reader.Read(3) + 2;
            return ReadStream(SubSampleSize(stored.xsize, stored.bits), SubSampleSize(stored.ysize, stored.bits), false, stored.data);
        }
        case 2:
            return true;
        case 3: {
            const int numColors = reader.Read(8) + 1;
            stored.bits = (numColors > 16) ? 0 : (numColors > 4) ? 1 : (numColors > 2) ? 2 : 3;
            std::vector<std::uint32_t> table;
            if (!ReadStream(numColors, 1, false, table)) {
                return false;
            }
            // The color table is written as the differences between neighboring entries, so the
            // entries are laid down one after another and each is the one before it plus its own
            // difference.
            for (std::size_t index = 1; index < table.size(); ++index) {
                std::uint32_t value = 0;
                for (int shift = 0; shift < 32; shift += 8) {
                    const std::uint32_t step = ((table[index] >> shift) & 0xff) + ((table[index - 1] >> shift) & 0xff);
                    value |= (step & 0xff) << shift;
                }
                table[index] = value;
            }
            stored.data = std::move(table);
            xsize = SubSampleSize(stored.xsize, stored.bits);
            return true;
        }
        default:
            return false;
    }
}

void ApplyColorIndexing(const Transform& transform, int& xsize, std::vector<std::uint32_t>& pixels) {
    const int bitsPerPixel = 8 >> transform.bits;
    const int pixelsPerByte = 1 << transform.bits;
    const int countMask = pixelsPerByte - 1;
    const int bitMask = (1 << bitsPerPixel) - 1;
    const int width = transform.xsize;
    const int height = transform.ysize;
    const int numColors = static_cast<int>(transform.data.size());
    std::vector<std::uint32_t> expanded(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    std::size_t source = 0;
    for (int y = 0; y < height; ++y) {
        std::uint32_t packed = 0;
        for (int x = 0; x < width; ++x) {
            if (bitsPerPixel < 8) {
                if ((x & countMask) == 0) {
                    packed = source < pixels.size() ? ((pixels[source] >> 8) & 0xff) : 0;
                    ++source;
                }
                const int index = static_cast<int>((packed >> ((x & countMask) * bitsPerPixel)) & static_cast<std::uint32_t>(bitMask));
                expanded[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                    index < numColors ? transform.data[static_cast<std::size_t>(index)] : 0;
            } else {
                const int index = static_cast<int>(source < pixels.size() ? ((pixels[source] >> 8) & 0xff) : 0);
                ++source;
                expanded[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                    index < numColors ? transform.data[static_cast<std::size_t>(index)] : 0;
            }
        }
    }
    pixels.swap(expanded);
    xsize = width;
}

void ApplyPredictor(const Transform& transform, std::vector<std::uint32_t>& pixels) {
    const int width = transform.xsize;
    const int height = transform.ysize;
    const int tilesPerRow = SubSampleSize(width, transform.bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tile = static_cast<int>((transform.data[static_cast<std::size_t>((y >> transform.bits) * tilesPerRow + (x >> transform.bits))] >> 8) & 0xff);
            std::uint32_t prediction = kPredictorBlack;
            if (x == 0 && y == 0) {
                prediction = kPredictorBlack;
            } else if (y == 0) {
                prediction = pixels[static_cast<std::size_t>(x - 1)];
            } else if (x == 0) {
                prediction = pixels[static_cast<std::size_t>(y - 1) * static_cast<std::size_t>(width)];
            } else {
                const std::uint32_t left = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x - 1)];
                const std::uint32_t top = pixels[static_cast<std::size_t>(y - 1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                const std::uint32_t topLeft = pixels[static_cast<std::size_t>(y - 1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x - 1)];
                // The pixel above and to the right of the last pixel of a row is not written yet, so
                // the format names the first pixel of the same row in its place.
                const std::uint32_t topRight = (x + 1 < width) ? pixels[static_cast<std::size_t>(y - 1) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x + 1)]
                                                               : pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width)];
                prediction = Predict(tile, left, top, topLeft, topRight);
            }
            const std::size_t position = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            pixels[position] = AddPixels(pixels[position], prediction);
        }
    }
}

void ApplyCrossColor(const Transform& transform, std::vector<std::uint32_t>& pixels) {
    const int width = transform.xsize;
    const int height = transform.ysize;
    const int tilesPerRow = SubSampleSize(width, transform.bits);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tile = (y >> transform.bits) * tilesPerRow + (x >> transform.bits);
            const std::size_t position = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            pixels[position] = ColorTransformInverse(transform.data[static_cast<std::size_t>(tile)], pixels[position]);
        }
    }
}

void ApplySubtractGreen(std::vector<std::uint32_t>& pixels) {
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::uint32_t argb = pixels[index];
        const std::uint32_t green = (argb >> 8) & 0xff;
        std::uint32_t redAndBlue = argb & 0x00ff00ffu;
        redAndBlue += (green << 16) | green;
        redAndBlue &= 0x00ff00ffu;
        pixels[index] = (argb & 0xff00ff00u) | redAndBlue;
    }
}

bool Vp8LReader::ReadStream(int xsize, int ysize, bool isLevel0, std::vector<std::uint32_t>& pixels) {
    if (xsize <= 0 || ysize <= 0) {
        pixels.clear();
        return true;
    }
    if (xsize > kMaxEdge || ysize > kMaxEdge || static_cast<long long>(xsize) * ysize > static_cast<long long>(kMaxEdge) * kMaxEdge) {
        return false;
    }
    int transformedWidth = xsize;
    int transformedHeight = ysize;
    if (isLevel0) {
        while (reader.Read(1)) {
            if (!ReadTransform(transformedWidth, transformedHeight)) {
                return false;
            }
        }
    }
    int colorCacheBits = 0;
    if (reader.Read(1)) {
        colorCacheBits = reader.Read(4);
        if (colorCacheBits < 1 || colorCacheBits > kMaxCacheBits) {
            return false;
        }
    }
    std::vector<HuffmanGroup> groups;
    std::vector<int> groupMap;
    int groupBits = 0;
    if (!ReadHuffmanCodes(transformedWidth, transformedHeight, colorCacheBits, isLevel0, groups, groupMap, groupBits)) {
        return false;
    }
    if (!ReadPixels(transformedWidth, transformedHeight, colorCacheBits, groups, groupMap, groupBits, pixels)) {
        return false;
    }
    // Undone last thing first, which is the order the transforms were read in reversed: the
    // innermost transform is the one nearest the coded pixels. Only the picture's own stream has
    // transforms; a subimage is plain pixels, and the transform still being read around it is not
    // one of its own and would be laid over a buffer far too small for it.
    for (int index = isLevel0 ? static_cast<int>(transforms.size()) - 1 : -1; index >= 0; --index) {
        const Transform& transform = transforms[static_cast<std::size_t>(index)];
        switch (transform.type) {
            case 0:
                ApplyPredictor(transform, pixels);
                break;
            case 1:
                ApplyCrossColor(transform, pixels);
                break;
            case 2:
                ApplySubtractGreen(pixels);
                break;
            case 3:
                ApplyColorIndexing(transform, transformedWidth, pixels);
                break;
            default:
                return false;
        }
    }
    if (isLevel0) {
        transforms.clear();
    }
    return true;
}

bool Vp8LReader::Read(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra) {
    if (data == nullptr || size < 5 || data[0] != 0x2f) {
        return false;
    }
    reader.Open(data, size);
    reader.Read(8);
    width = reader.Read(14) + 1;
    height = reader.Read(14) + 1;
    reader.Read(1);
    if (reader.Read(3) != 0) {
        return false;
    }
    if (width < 1 || height < 1 || width > kMaxEdge || height > kMaxEdge) {
        return false;
    }
    std::vector<std::uint32_t> pixels;
    if (!ReadStream(width, height, true, pixels)) {
        return false;
    }
    if (pixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }
    bgra.assign(pixels.size() * 4, 255);
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        const std::uint32_t argb = pixels[index];
        bgra[index * 4] = static_cast<unsigned char>(argb & 0xff);
        bgra[index * 4 + 1] = static_cast<unsigned char>((argb >> 8) & 0xff);
        bgra[index * 4 + 2] = static_cast<unsigned char>((argb >> 16) & 0xff);
        bgra[index * 4 + 3] = static_cast<unsigned char>((argb >> 24) & 0xff);
    }
    return true;
}

}  // namespace

bool DecodeVp8LStream(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra) {
    Vp8LReader decoder;
    return decoder.Read(data, size, width, height, bgra);
}

}  // namespace Runtime
