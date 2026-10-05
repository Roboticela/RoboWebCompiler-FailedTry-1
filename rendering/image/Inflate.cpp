#include "rendering/image/Inflate.h"

namespace Runtime {
namespace {

constexpr int kMaxOutput = 32 * 1024 * 1024;

const int kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistanceBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const int kCodeOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct BitIn {
    const unsigned char* data = nullptr;
    int size = 0;
    int index = 0;
    unsigned int buffer = 0;
    int bits = 0;

    bool Pull(int count, unsigned int& value) {
        while (bits < count) {
            if (index >= size) {
                return false;
            }
            buffer |= static_cast<unsigned int>(data[index++]) << bits;
            bits += 8;
        }
        value = buffer & ((1u << count) - 1u);
        buffer >>= count;
        bits -= count;
        return true;
    }

    void Align() {
        const int drop = bits & 7;
        unsigned int unused = 0;
        if (drop > 0) {
            Pull(drop, unused);
        }
    }
};

struct Huffman {
    int counts[16] = {};
    int minCode[16] = {};
    int maxCode[16] = {};
    int offset[16] = {};
    int symbols[320] = {};

    bool Build(const int* lengths, int count) {
        int blCount[16] = {};
        for (int symbol = 0; symbol < count; ++symbol) {
            const int length = lengths[symbol];
            if (length < 0 || length > 15) {
                return false;
            }
            if (length > 0) {
                ++blCount[length];
            }
        }
        int nextCode[16] = {};
        int code = 0;
        for (int length = 1; length <= 15; ++length) {
            code = (code + blCount[length - 1]) << 1;
            nextCode[length] = code;
            minCode[length] = code;
            maxCode[length] = code + blCount[length] - 1;
            counts[length] = blCount[length];
        }
        int cursor = 0;
        for (int length = 1; length <= 15; ++length) {
            offset[length] = cursor;
            cursor += blCount[length];
        }
        int placed[16] = {};
        for (int symbol = 0; symbol < count; ++symbol) {
            const int length = lengths[symbol];
            if (length == 0) {
                continue;
            }
            symbols[offset[length] + placed[length]] = symbol;
            ++placed[length];
        }
        return true;
    }

    int Decode(BitIn& input) const {
        int code = 0;
        for (int length = 1; length <= 15; ++length) {
            unsigned int bit = 0;
            if (!input.Pull(1, bit)) {
                return -1;
            }
            code = (code << 1) | static_cast<int>(bit);
            if (counts[length] > 0 && code <= maxCode[length]) {
                return symbols[offset[length] + (code - minCode[length])];
            }
        }
        return -1;
    }
};

bool BuildFixed(Huffman& literal, Huffman& distance) {
    int lengths[288];
    for (int symbol = 0; symbol <= 143; ++symbol) {
        lengths[symbol] = 8;
    }
    for (int symbol = 144; symbol <= 255; ++symbol) {
        lengths[symbol] = 9;
    }
    for (int symbol = 256; symbol <= 279; ++symbol) {
        lengths[symbol] = 7;
    }
    for (int symbol = 280; symbol <= 287; ++symbol) {
        lengths[symbol] = 8;
    }
    int distances[32];
    for (int symbol = 0; symbol < 32; ++symbol) {
        distances[symbol] = 5;
    }
    return literal.Build(lengths, 288) && distance.Build(distances, 32);
}

bool DecodeCodes(BitIn& input, Huffman& literal, Huffman& distance, std::vector<unsigned char>& output) {
    for (;;) {
        const int symbol = literal.Decode(input);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 256) {
            if (static_cast<int>(output.size()) >= kMaxOutput) {
                return false;
            }
            output.push_back(static_cast<unsigned char>(symbol));
            continue;
        }
        if (symbol == 256) {
            return true;
        }
        const int lengthIndex = symbol - 257;
        if (lengthIndex < 0 || lengthIndex > 28) {
            return false;
        }
        unsigned int extra = 0;
        if (kLengthExtra[lengthIndex] > 0 && !input.Pull(kLengthExtra[lengthIndex], extra)) {
            return false;
        }
        const int length = kLengthBase[lengthIndex] + static_cast<int>(extra);
        const int distanceSymbol = distance.Decode(input);
        if (distanceSymbol < 0 || distanceSymbol > 29) {
            return false;
        }
        extra = 0;
        if (kDistanceExtra[distanceSymbol] > 0 && !input.Pull(kDistanceExtra[distanceSymbol], extra)) {
            return false;
        }
        const int distance = kDistanceBase[distanceSymbol] + static_cast<int>(extra);
        if (distance <= 0 || distance > static_cast<int>(output.size())) {
            return false;
        }
        for (int count = 0; count < length; ++count) {
            if (static_cast<int>(output.size()) >= kMaxOutput) {
                return false;
            }
            output.push_back(output[output.size() - static_cast<std::size_t>(distance)]);
        }
    }
}

bool DecodeDynamic(BitIn& input, Huffman& literal, Huffman& distance) {
    unsigned int hlit = 0;
    unsigned int hdist = 0;
    unsigned int hclen = 0;
    if (!input.Pull(5, hlit) || !input.Pull(5, hdist) || !input.Pull(4, hclen)) {
        return false;
    }
    const int literalCount = static_cast<int>(hlit) + 257;
    const int distanceCount = static_cast<int>(hdist) + 1;
    const int codeCount = static_cast<int>(hclen) + 4;
    int codeLengths[19] = {};
    for (int index = 0; index < codeCount; ++index) {
        unsigned int length = 0;
        if (!input.Pull(3, length)) {
            return false;
        }
        codeLengths[kCodeOrder[index]] = static_cast<int>(length);
    }
    Huffman codes;
    if (!codes.Build(codeLengths, 19)) {
        return false;
    }
    int lengths[320] = {};
    int filled = 0;
    const int total = literalCount + distanceCount;
    while (filled < total) {
        const int symbol = codes.Decode(input);
        if (symbol < 0) {
            return false;
        }
        if (symbol < 16) {
            lengths[filled++] = symbol;
            continue;
        }
        unsigned int extra = 0;
        int repeat = 0;
        int value = 0;
        if (symbol == 16) {
            if (filled == 0 || !input.Pull(2, extra)) {
                return false;
            }
            repeat = 3 + static_cast<int>(extra);
            value = lengths[filled - 1];
        } else if (symbol == 17) {
            if (!input.Pull(3, extra)) {
                return false;
            }
            repeat = 3 + static_cast<int>(extra);
        } else if (symbol == 18) {
            if (!input.Pull(7, extra)) {
                return false;
            }
            repeat = 11 + static_cast<int>(extra);
        } else {
            return false;
        }
        if (filled + repeat > total) {
            return false;
        }
        for (int count = 0; count < repeat; ++count) {
            lengths[filled++] = value;
        }
    }
    return literal.Build(lengths, literalCount) && distance.Build(lengths + literalCount, distanceCount);
}

bool InflateBlocks(BitIn& input, std::vector<unsigned char>& output) {
    bool finalBlock = false;
    while (!finalBlock) {
        unsigned int finalBit = 0;
        unsigned int type = 0;
        if (!input.Pull(1, finalBit) || !input.Pull(2, type)) {
            return false;
        }
        finalBlock = finalBit == 1;
        if (type == 0) {
            input.Align();
            if (input.index + 4 > input.size) {
                return false;
            }
            const int length = input.data[input.index] | (input.data[input.index + 1] << 8);
            const int complement = input.data[input.index + 2] | (input.data[input.index + 3] << 8);
            input.index += 4;
            input.buffer = 0;
            input.bits = 0;
            if ((length ^ 0xFFFF) != complement || input.index + length > input.size) {
                return false;
            }
            output.insert(output.end(), input.data + input.index, input.data + input.index + length);
            input.index += length;
        } else if (type == 1 || type == 2) {
            Huffman literal;
            Huffman distance;
            const bool built = type == 1 ? BuildFixed(literal, distance) : DecodeDynamic(input, literal, distance);
            if (!built || !DecodeCodes(input, literal, distance, output)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool InflateRawBytes(const unsigned char* data, int size, std::vector<unsigned char>& output) {
    output.clear();
    if (data == nullptr || size < 1) {
        return false;
    }
    BitIn input;
    input.data = data;
    input.size = size;
    return InflateBlocks(input, output) && !output.empty();
}

}  // namespace

bool InflateRaw(const unsigned char* data, int size, std::vector<unsigned char>& output) {
    return InflateRawBytes(data, size, output);
}

bool InflateZlib(const unsigned char* data, int size, std::vector<unsigned char>& output) {
    output.clear();
    if (data == nullptr || size < 2) {
        return false;
    }
    int offset = 0;
    if ((data[0] & 0x0F) == 8) {
        const int window = (data[0] >> 4) & 0x0F;
        if (window > 7 || ((data[0] << 8) + data[1]) % 31 != 0) {
            return false;
        }
        offset = 2;
        if ((data[1] & 0x20) != 0) {
            if (size < 6) {
                return false;
            }
            offset = 6;
        }
    }
    BitIn input;
    input.data = data + offset;
    input.size = size - offset;
    if (!InflateBlocks(input, output)) {
        return false;
    }
    return !output.empty();
}

}  // namespace Runtime
