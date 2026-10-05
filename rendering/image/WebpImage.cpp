#include "rendering/image/ImageCodec.h"

#include <cstring>
#include <vector>

namespace Runtime {
namespace {

#include "rendering/image/Vp8Tables.inc"

const int kBands[16] = {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7};
const int kZig[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
const int kYTree[8] = {-4, 2, 4, 6, 0, -1, -2, -3};
const int kUvTree[6] = {0, 2, -1, 4, -2, -3};
const int kBTree[18] = {0, 2, -1, 4, -2, 6, 8, 12, -3, 10, -5, -6, -4, 14, -7, 16, -8, -9};

struct BoolReader {
    const unsigned char* data = nullptr;
    int size = 0;
    int position = 0;
    unsigned int range = 255;
    unsigned int value = 0;
    int count = 0;

    bool Open(const unsigned char* bytes, int length) {
        if (bytes == nullptr || length < 2) {
            return false;
        }
        data = bytes;
        size = length;
        position = 2;
        range = 255;
        value = (static_cast<unsigned int>(bytes[0]) << 8) | bytes[1];
        count = 0;
        return true;
    }

    int Read(int probability) {
        const unsigned int split = 1u + (((range - 1u) * static_cast<unsigned int>(probability)) >> 8);
        int bit = 0;
        if (value >= (split << 8)) {
            range -= split;
            value -= split << 8;
            bit = 1;
        } else {
            range = split;
        }
        while (range < 128) {
            value <<= 1;
            range <<= 1;
            ++count;
            if (count == 8) {
                count = 0;
                if (position < size) {
                    value |= data[position++];
                }
            }
        }
        return bit;
    }

    int Literal(int bits) {
        int result = 0;
        for (int index = 0; index < bits; ++index) {
            result = (result << 1) | Read(128);
        }
        return result;
    }

    int Signed(int bits) {
        const int magnitude = Literal(bits);
        return Read(128) ? -magnitude : magnitude;
    }

    int Tree(const int* tree, const int* probability, int start = 0) {
        int index = start;
        for (;;) {
            const int next = Read(probability[index / 2]) ? tree[index + 1] : tree[index];
            if (next <= 0) {
                return -next;
            }
            index = next;
        }
    }
};

int ClampQ(int value) {
    if (value < 0) {
        return 0;
    }
    if (value > 127) {
        return 127;
    }
    return value;
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

const int* CoefficientProbabilities(const int* probabilities, int plane, int band, int context) {
    return probabilities + ((plane * 8 + band) * 3 + context) * 11;
}

int LargeCoefficient(BoolReader& reader, const int* probability) {
    if (reader.Read(probability[3]) == 0) {
        if (reader.Read(probability[4]) == 0) {
            return 2;
        }
        return 3 + reader.Read(probability[5]);
    }
    if (reader.Read(probability[6]) == 0) {
        if (reader.Read(probability[7]) == 0) {
            return 5 + reader.Read(159);
        }
        return 7 + 2 * reader.Read(165) + reader.Read(145);
    }
    const int bit1 = reader.Read(probability[8]);
    const int bit0 = reader.Read(probability[9 + bit1]);
    const int category = 2 * bit1 + bit0;
    const int extras[4][11] = {{173, 148, 140},
                               {176, 155, 140, 135},
                               {180, 157, 141, 134, 130},
                               {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129}};
    const int extraCount[4] = {3, 4, 5, 11};
    int value = 0;
    for (int bit = 0; bit < extraCount[category]; ++bit) {
        value = (value << 1) | reader.Read(extras[category][bit]);
    }
    return value + 3 + (8 << category);
}

int ReadCoefficients(BoolReader& reader, const int* probabilities, int plane, int context, int start, int dc, int ac, int block[16]) {
    for (int index = 0; index < 16; ++index) {
        block[index] = 0;
    }
    int index = start;
    const int* table = CoefficientProbabilities(probabilities, plane, kBands[index], context);
    while (index < 16) {
        if (reader.Read(table[0]) == 0) {
            return index;
        }
        while (reader.Read(table[1]) == 0) {
            ++index;
            if (index == 16) {
                return 16;
            }
            table = CoefficientProbabilities(probabilities, plane, kBands[index], 0);
        }
        int value = 1;
        int nextContext = 1;
        if (reader.Read(table[2]) != 0) {
            value = LargeCoefficient(reader, table);
            nextContext = 2;
        }
        if (reader.Read(128) != 0) {
            value = -value;
        }
        block[kZig[index]] = value * (index == 0 ? dc : ac);
        ++index;
        if (index < 16) {
            table = CoefficientProbabilities(probabilities, plane, kBands[index], nextContext);
        }
    }
    return 16;
}

void InverseWalsh(const int input[16], int output[16]) {
    int temp[16];
    for (int index = 0; index < 4; ++index) {
        const int a0 = input[index] + input[12 + index];
        const int a1 = input[4 + index] + input[8 + index];
        const int a2 = input[4 + index] - input[8 + index];
        const int a3 = input[index] - input[12 + index];
        temp[index] = a0 + a1;
        temp[8 + index] = a0 - a1;
        temp[4 + index] = a3 + a2;
        temp[12 + index] = a3 - a2;
    }
    for (int index = 0; index < 4; ++index) {
        const int dc = temp[index * 4] + 3;
        const int a0 = dc + temp[index * 4 + 3];
        const int a1 = temp[index * 4 + 1] + temp[index * 4 + 2];
        const int a2 = temp[index * 4 + 1] - temp[index * 4 + 2];
        const int a3 = dc - temp[index * 4 + 3];
        output[index] = (a0 + a1) >> 3;
        output[4 + index] = (a3 + a2) >> 3;
        output[8 + index] = (a0 - a1) >> 3;
        output[12 + index] = (a3 - a2) >> 3;
    }
}

void AddTransform(const int coeffs[16], unsigned char* pixels, int stride) {
    const int kC1 = 20091;
    const int kC2 = 35468;
    bool onlyDc = true;
    for (int index = 1; index < 16; ++index) {
        if (coeffs[index] != 0) {
            onlyDc = false;
            break;
        }
    }
    if (onlyDc) {
        const int delta = (coeffs[0] + 4) >> 3;
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                pixels[y * stride + x] = static_cast<unsigned char>(ClampByte(pixels[y * stride + x] + delta));
            }
        }
        return;
    }
    int temp[16];
    for (int index = 0; index < 4; ++index) {
        const int a = coeffs[index] + coeffs[8 + index];
        const int b = coeffs[index] - coeffs[8 + index];
        const int c = ((coeffs[4 + index] * kC2) >> 16) - ((((coeffs[12 + index] * kC1) >> 16) + coeffs[12 + index]));
        const int d = (((coeffs[4 + index] * kC1) >> 16) + coeffs[4 + index]) + ((coeffs[12 + index] * kC2) >> 16);
        temp[index * 4] = a + d;
        temp[index * 4 + 1] = b + c;
        temp[index * 4 + 2] = b - c;
        temp[index * 4 + 3] = a - d;
    }
    for (int index = 0; index < 4; ++index) {
        const int dc = temp[index] + 4;
        const int a = dc + temp[8 + index];
        const int b = dc - temp[8 + index];
        const int c = ((temp[4 + index] * kC2) >> 16) - ((((temp[12 + index] * kC1) >> 16) + temp[12 + index]));
        const int d = (((temp[4 + index] * kC1) >> 16) + temp[4 + index]) + ((temp[12 + index] * kC2) >> 16);
        const int row[4] = {(a + d) >> 3, (b + c) >> 3, (b - c) >> 3, (a - d) >> 3};
        for (int x = 0; x < 4; ++x) {
            pixels[index * stride + x] = static_cast<unsigned char>(ClampByte(pixels[index * stride + x] + row[x]));
        }
    }
}

int YuvChannel(int value) {
    const int mask = (256 << 6) - 1;
    if ((value & ~mask) == 0) {
        return value >> 6;
    }
    return value < 0 ? 0 : 255;
}

}  // namespace

bool DecodeWebpImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 20 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WEBP", 4) != 0) {
        return false;
    }
    if (std::memcmp(data + 12, "VP8 ", 4) != 0) {
        return false;
    }
    const int chunkSize = data[16] | (data[17] << 8) | (data[18] << 16);
    if (chunkSize < 10 || 20 + chunkSize > size) {
        return false;
    }
    const unsigned char* payload = data + 20;
    const int tag = payload[0] | (payload[1] << 8) | (payload[2] << 16);
    if ((tag & 1) != 0 || payload[3] != 0x9D || payload[4] != 0x01 || payload[5] != 0x2A) {
        return false;
    }
    const int width = payload[6] | ((payload[7] & 0x3F) << 8);
    const int height = payload[8] | ((payload[9] & 0x3F) << 8);
    const int partitionSize = tag >> 5;
    if (width < 1 || height < 1 || width > 16 || height > 16 || 10 + partitionSize >= chunkSize) {
        return false;
    }
    BoolReader header;
    if (!header.Open(payload + 10, partitionSize)) {
        return false;
    }
    header.Read(128);
    header.Read(128);
    if (header.Read(128)) {
        return false;
    }
    header.Read(128);
    header.Literal(6);
    header.Literal(3);
    if (header.Read(128)) {
        return false;
    }
    const int partitions = 1 << header.Literal(2);
    if (partitions != 1) {
        return false;
    }
    const int quant = header.Literal(7);
    int delta[5];
    for (int index = 0; index < 5; ++index) {
        delta[index] = header.Read(128) ? header.Signed(4) : 0;
    }
    header.Read(128);
    int probabilities[1056];
    for (int index = 0; index < 1056; ++index) {
        probabilities[index] = kCoeffProb[index];
        if (header.Read(kCoeffUpdate[index])) {
            probabilities[index] = header.Literal(8);
        }
    }
    const bool skipPresent = header.Read(128) != 0;
    const int skipProbability = skipPresent ? header.Literal(8) : 0;
    const int yModeProbs[4] = {145, 156, 163, 128};
    const int uvModeProbs[3] = {142, 114, 183};
    const bool skipBlock = skipPresent && header.Read(skipProbability) != 0;
    const int yMode = header.Tree(kYTree, yModeProbs);
    int submodes[16] = {};
    if (yMode == 4) {
        for (int block = 0; block < 16; ++block) {
            const int above = block >= 4 ? submodes[block - 4] : 0;
            const int left = (block % 4) != 0 ? submodes[block - 1] : 0;
            submodes[block] = header.Tree(kBTree, kBmodeProb + (above * 10 + left) * 9);
        }
    }
    header.Tree(kUvTree, uvModeProbs);
    BoolReader tokens;
    if (!tokens.Open(payload + 10 + partitionSize, chunkSize - 10 - partitionSize)) {
        return false;
    }
    const int yAc = kAcQ[ClampQ(quant)];
    int uvIndex = quant + delta[3];
    if (uvIndex < 0) {
        uvIndex = 0;
    }
    if (uvIndex > 117) {
        uvIndex = 117;
    }
    const int uvDc = kDcQ[uvIndex];
    const int uvAc = kAcQ[ClampQ(quant + delta[4])];
    unsigned char yPlane[16 * 16];
    unsigned char uPlane[8 * 8];
    unsigned char vPlane[8 * 8];
    for (int index = 0; index < 256; ++index) {
        yPlane[index] = 127;
    }
    for (int index = 0; index < 64; ++index) {
        uPlane[index] = 127;
        vPlane[index] = 127;
    }
    if (!skipBlock && yMode != 4) {
        int y2[16];
        const int y2Dc = kDcQ[ClampQ(quant + delta[1])] * 2;
        int y2Ac = (kAcQ[ClampQ(quant + delta[2])] * 101581) >> 16;
        if (y2Ac < 8) {
            y2Ac = 8;
        }
        ReadCoefficients(tokens, probabilities, 1, 0, 0, y2Dc, y2Ac, y2);
        int dcValues[16];
        InverseWalsh(y2, dcValues);
        int topNonZero = 0;
        int leftNonZero = 0;
        for (int row = 0; row < 4; ++row) {
            int leftFlag = leftNonZero & 1;
            for (int column = 0; column < 4; ++column) {
                int residual[16];
                const int end = ReadCoefficients(tokens, probabilities, 0, leftFlag + (topNonZero & 1), 1, 0, yAc, residual);
                residual[0] = dcValues[row * 4 + column];
                AddTransform(residual, yPlane + (row * 4) * 16 + column * 4, 16);
                leftFlag = end > 1 ? 1 : 0;
                topNonZero = ((topNonZero >> 1) | (leftFlag << 7)) & 255;
            }
            topNonZero >>= 4;
            leftNonZero = ((leftNonZero >> 1) | (leftFlag << 7)) & 255;
        }
    } else if (!skipBlock) {
        int topNonZero = 0;
        int leftNonZero = 0;
        for (int row = 0; row < 4; ++row) {
            int leftFlag = leftNonZero & 1;
            for (int column = 0; column < 4; ++column) {
                int residual[16];
                const int end = ReadCoefficients(tokens, probabilities, 3, leftFlag + (topNonZero & 1), 0, kDcQ[ClampQ(quant + delta[0])], yAc, residual);
                AddTransform(residual, yPlane + (row * 4) * 16 + column * 4, 16);
                leftFlag = end > 0 ? 1 : 0;
                topNonZero = ((topNonZero >> 1) | (leftFlag << 7)) & 255;
            }
            topNonZero >>= 4;
            leftNonZero = ((leftNonZero >> 1) | (leftFlag << 7)) & 255;
        }
    }
    if (!skipBlock) {
        for (int channel = 0; channel < 2; ++channel) {
            unsigned char* chroma = channel == 0 ? uPlane : vPlane;
            int topNonZero = 0;
            int leftNonZero = 0;
            for (int row = 0; row < 2; ++row) {
                int leftFlag = leftNonZero & 1;
                for (int column = 0; column < 2; ++column) {
                    int residual[16];
                    const int end = ReadCoefficients(tokens, probabilities, 2, leftFlag + (topNonZero & 1), 0, uvDc, uvAc, residual);
                    AddTransform(residual, chroma + (row * 4) * 8 + column * 4, 8);
                    leftFlag = end > 0 ? 1 : 0;
                    topNonZero = ((topNonZero >> 1) | (leftFlag << 3)) & 255;
                }
                topNonZero >>= 2;
                leftNonZero = ((leftNonZero >> 1) | (leftFlag << 5)) & 255;
            }
        }
    }
    ImageFrame frame;
    frame.width = width;
    frame.height = height;
    frame.bgra.assign(static_cast<std::size_t>(width * height * 4), 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int luma = yPlane[y * 16 + x];
            const int cb = uPlane[(y / 2) * 8 + (x / 2)];
            const int cr = vPlane[(y / 2) * 8 + (x / 2)];
            const int red = YuvChannel(((luma * 19077) >> 8) + ((cr * 26149) >> 8) - 14234);
            const int green = YuvChannel(((luma * 19077) >> 8) - ((cb * 6419) >> 8) - ((cr * 13320) >> 8) + 8708);
            const int blue = YuvChannel(((luma * 19077) >> 8) + ((cb * 33050) >> 8) - 17685);
            const int pixel = (y * width + x) * 4;
            frame.bgra[static_cast<std::size_t>(pixel)] = static_cast<unsigned char>(blue);
            frame.bgra[static_cast<std::size_t>(pixel + 1)] = static_cast<unsigned char>(green);
            frame.bgra[static_cast<std::size_t>(pixel + 2)] = static_cast<unsigned char>(red);
        }
    }
    image.format = "webp";
    image.frames.push_back(std::move(frame));
    return true;
}

}  // namespace Runtime
