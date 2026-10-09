#include "rendering/image/Vp8Image.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace Runtime {
namespace {

const int kMaxEdge = 8192;
const int kMaxCoefficients = 400;
const int kNumSegments = 4;

#include "rendering/image/Vp8Tables.inc"

// The codings of the modes a macroblock can be predicted in, and of the parts of a macroblock that
// predicts each of its sixteen quarters on its own. Each tree is written flat: an entry at an even
// position is where a zero bit goes and the entry beside it is where a one goes, a positive value is
// another position in the tree, and a value of zero or less is a leaf holding the negated mode.
const int kYModeTree[8] = {-4, 2, 4, 6, 0, -1, -2, -3};
const int kYModeProbabilities[4] = {145, 156, 163, 128};
const int kUvModeTree[6] = {0, 2, -1, 4, -2, -3};
const int kUvModeProbabilities[3] = {142, 114, 183};
// The ten quarter-block predictions and the tree that codes them. The three diagonals hanging off
// the first branch sit in the order the format writes them, which is not the order of their names.
const int kBmodeTree[18] = {0, 2, -1, 4, -2, 6, 8, 12, -3, 10, -5, -6, -4, 14, -7, 16, -8, -9};
const int kSegmentTree[6] = {2, 4, 0, -1, -2, -3};

// Which probability table a coefficient position is coded with. The coefficients near the top-left
// of a block change together from block to block, so each of those positions gets a table of its own;
// the ones further out share the last table between them.
const int kBands[16] = {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7};
const int kZigZag[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};

// Which of the six dequantization figures a block needs.
const int kCoarseDc = 0;
const int kLumaDc = 1;
const int kChromaDc = 2;

int Clip(int value, int low, int high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int Magnitude(int value) {
    return value < 0 ? -value : value;
}

// Positions along one axis of a colour plane, kept inside it. A pixel on the very edge of the picture
// has no sample beyond it to lean on, so the one it does have stands in for its absent neighbour.
int ClampPlaneIndex(int index, int extent) {
    return index < 0 ? 0 : (index >= extent ? extent - 1 : index);
}

// Spreads one colour sample over a square of four pixels. The sample nearest the pixel is taken at
// three times the weight of the one beside it, and the sample one place along the diagonal carries a
// quarter, which comes to nine, three, three and one parts in sixteen. The two rounds of the sum are
// kept apart so that the arithmetic matches the format's own conversion step for step.
int SpreadChromaSample(int near, int alongOneAxis, int alongOtherAxis, int diagonal) {
    const int average = near + alongOneAxis + alongOtherAxis + diagonal + 8 + 2 * (alongOneAxis + alongOtherAxis);
    return ((average >> 3) + near) >> 1;
}

// The bits of a partition. A value is coded as a place inside a span that narrows with every value
// read; the reader carries the width of the span and how far into it the coded number reaches, cuts
// the same span the encoder cut, and shifts in more bits as the span grows too small to tell one
// value from another. Both sides must make the cuts in the same order or they drift apart.
struct BoolReader {
    const unsigned char* data = nullptr;
    int size = 0;
    int index = 0;
    std::uint32_t range = 255;
    std::uint32_t value = 0;
    int bitCount = 0;

    bool Open(const unsigned char* bytes, int length) {
        if (bytes == nullptr || length < 2) {
            return false;
        }
        data = bytes;
        size = length;
        index = 2;
        value = (static_cast<std::uint32_t>(bytes[0]) << 8) | bytes[1];
        range = 255;
        bitCount = 0;
        return true;
    }

    int Read(int probability) {
        const std::uint32_t split = 1u + (((range - 1u) * static_cast<std::uint32_t>(probability)) >> 8);
        const std::uint32_t scaled = split << 8;
        int bit = 0;
        if (value >= scaled) {
            bit = 1;
            range -= split;
            value -= scaled;
        } else {
            range = split;
        }
        while (range < 128) {
            value <<= 1;
            range <<= 1;
            if (++bitCount == 8) {
                bitCount = 0;
                value |= (index < size) ? data[index++] : 0;
            }
        }
        return bit;
    }

    // A plain number of bits, most significant first.
    int Literal(int bits) {
        int result = 0;
        while (bits-- > 0) {
            result = (result << 1) + Read(128);
        }
        return result;
    }

    // A magnitude followed by its sign, as the format writes every signed header field.
    int SignedLiteral(int bits) {
        const int magnitude = Literal(bits);
        return Read(128) ? -magnitude : magnitude;
    }

    int Tree(const int* tree, const int* probabilities) {
        int position = 0;
        while ((position = tree[position + Read(probabilities[position >> 1])]) > 0) {
        }
        return -position;
    }
};

int Average2(int first, int second) {
    return (first + second + 1) >> 1;
}

int Average3(int first, int second, int third) {
    return (first + second + second + third + 2) >> 2;
}

// The pixels of one quarter of a macroblock, guessed from the row above it, the column to its left
// and the pixel between the two. The row is eight wide because four of them lie above the pixels to
// the right of the quarter, and half the modes reach into those.
void PredictBlock(int mode, const int above[8], const int left[4], int corner, int out[16]) {
    int aboveExtended[10];
    aboveExtended[0] = corner;
    for (int index = 0; index < 8; ++index) {
        aboveExtended[index + 1] = above[index];
    }
    int leftExtended[6];
    leftExtended[0] = corner;
    for (int index = 0; index < 4; ++index) {
        leftExtended[index + 1] = left[index];
    }
    int edge[9];
    edge[0] = left[3];
    edge[1] = left[2];
    edge[2] = left[1];
    edge[3] = left[0];
    edge[4] = corner;
    for (int index = 0; index < 4; ++index) {
        edge[5 + index] = above[index];
    }
    auto aboveAverage2 = [&](int position) { return Average2(aboveExtended[position], aboveExtended[position + 1]); };
    auto aboveAverage3 = [&](int position) { return Average3(aboveExtended[position], aboveExtended[position + 1], aboveExtended[position + 2]); };
    auto edgeAverage3 = [&](int position) { return Average3(edge[position], edge[position + 1], edge[position + 2]); };
    for (int index = 0; index < 16; ++index) {
        out[index] = 0;
    }
    switch (mode) {
        case 0: {
            int total = 4;
            for (int index = 0; index < 4; ++index) {
                total += above[index] + left[index];
            }
            const int value = total >> 3;
            for (int index = 0; index < 16; ++index) {
                out[index] = value;
            }
            break;
        }
        case 1:
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    out[row * 4 + column] = Clip(leftExtended[row + 1] + aboveExtended[column + 1] - corner, 0, 255);
                }
            }
            break;
        case 2:
            for (int column = 0; column < 4; ++column) {
                const int value = aboveAverage3(column);
                for (int row = 0; row < 4; ++row) {
                    out[row * 4 + column] = value;
                }
            }
            break;
        case 3:
            for (int row = 0; row < 4; ++row) {
                // The first and last rows reach one pixel beyond the column, into the corner above
                // and the pixel below the block, which is a copy of the last one.
                const int first = row == 0 ? corner : left[row - 1];
                const int third = row == 3 ? left[3] : left[row + 1];
                const int value = Average3(first, left[row], third);
                for (int column = 0; column < 4; ++column) {
                    out[row * 4 + column] = value;
                }
            }
            break;
        case 4:
            // The first diagonal leans on the row above starting one pixel in, not on the corner;
            // the last one is spelled out because it reaches one pixel past the end of the row.
            out[0] = aboveAverage3(1);
            out[1] = out[4] = aboveAverage3(2);
            out[2] = out[5] = out[8] = aboveAverage3(3);
            out[3] = out[6] = out[9] = out[12] = aboveAverage3(4);
            out[7] = out[10] = out[13] = aboveAverage3(5);
            out[11] = out[14] = aboveAverage3(6);
            out[15] = Average3(above[6], above[7], above[7]);
            break;
        case 5:
            out[12] = edgeAverage3(0);
            out[13] = out[8] = edgeAverage3(1);
            out[14] = out[9] = out[4] = edgeAverage3(2);
            out[15] = out[10] = out[5] = out[0] = edgeAverage3(3);
            out[11] = out[6] = out[1] = edgeAverage3(4);
            out[7] = out[2] = edgeAverage3(5);
            out[3] = edgeAverage3(6);
            break;
        case 6: {
            const int pred0 = aboveAverage2(0);
            const int pred1 = aboveAverage2(1);
            const int pred2 = aboveAverage2(2);
            const int pred3 = aboveAverage2(3);
            const int pred4 = Average3(left[0], corner, above[0]);
            const int pred5 = aboveAverage3(0);
            const int pred6 = aboveAverage3(1);
            const int pred7 = aboveAverage3(2);
            const int pred8 = Average3(left[1], left[0], corner);
            const int pred9 = Average3(left[2], left[1], left[0]);
            out[0] = pred0;
            out[1] = pred1;
            out[2] = pred2;
            out[3] = pred3;
            out[4] = pred4;
            out[5] = pred5;
            out[6] = pred6;
            out[7] = pred7;
            out[8] = pred8;
            out[9] = pred0;
            out[10] = pred1;
            out[11] = pred2;
            out[12] = pred9;
            out[13] = pred4;
            out[14] = pred5;
            out[15] = pred6;
            break;
        }
        case 7: {
            const int pred0 = aboveAverage2(1);
            const int pred1 = aboveAverage2(2);
            const int pred2 = aboveAverage2(3);
            const int pred3 = aboveAverage2(4);
            const int pred4 = aboveAverage3(1);
            const int pred5 = aboveAverage3(2);
            const int pred6 = aboveAverage3(3);
            const int pred7 = aboveAverage3(4);
            const int pred8 = aboveAverage3(5);
            const int pred9 = aboveAverage3(6);
            out[0] = pred0;
            out[1] = pred1;
            out[2] = pred2;
            out[3] = pred3;
            out[4] = pred4;
            out[5] = pred5;
            out[6] = pred6;
            out[7] = pred7;
            out[8] = pred1;
            out[9] = pred2;
            out[10] = pred3;
            out[11] = pred8;
            out[12] = pred5;
            out[13] = pred6;
            out[14] = pred7;
            out[15] = pred9;
            break;
        }
        case 8:
            out[12] = Average2(edge[0], edge[1]);
            out[13] = edgeAverage3(0);
            out[8] = out[14] = Average2(edge[1], edge[2]);
            out[9] = out[15] = edgeAverage3(1);
            out[10] = out[4] = Average2(edge[2], edge[3]);
            out[11] = out[5] = edgeAverage3(2);
            out[6] = out[0] = Average2(edge[3], edge[4]);
            out[7] = out[1] = edgeAverage3(3);
            out[2] = edgeAverage3(4);
            out[3] = edgeAverage3(5);
            break;
        default:
            out[0] = Average2(left[0], left[1]);
            out[1] = Average3(leftExtended[1], leftExtended[2], leftExtended[3]);
            out[2] = out[4] = Average2(left[1], left[2]);
            out[3] = out[5] = Average3(leftExtended[2], leftExtended[3], leftExtended[4]);
            out[6] = out[8] = Average2(left[2], left[3]);
            out[7] = out[9] = Average3(left[2], left[3], left[3]);
            for (int index = 10; index < 16; ++index) {
                out[index] = left[3];
            }
            break;
    }
}

// A whole block guessed from the row above and the column to the left in one of the four plain ways.
// Unlike the quarter-block modes these never reach past the block itself, so the row and column are
// exactly as wide as the block.
void PredictWhole(int mode, int size, bool haveAbove, bool haveLeft, const int above[], const int left[], int corner, int out[]) {
    const int logSize = size == 16 ? 4 : 3;
    switch (mode) {
        case 0: {
            int total = 0;
            int shift = 0;
            if (haveAbove && haveLeft) {
                for (int index = 0; index < size; ++index) {
                    total += above[index] + left[index];
                }
                shift = logSize + 1;
            } else if (haveAbove) {
                for (int index = 0; index < size; ++index) {
                    total += above[index];
                }
                shift = logSize;
            } else if (haveLeft) {
                for (int index = 0; index < size; ++index) {
                    total += left[index];
                }
                shift = logSize;
            } else {
                for (int index = 0; index < size * size; ++index) {
                    out[index] = 128;
                }
                return;
            }
            const int value = (total + (1 << (shift - 1))) >> shift;
            for (int index = 0; index < size * size; ++index) {
                out[index] = value;
            }
            break;
        }
        case 1:
            for (int row = 0; row < size; ++row) {
                for (int column = 0; column < size; ++column) {
                    out[row * size + column] = above[column];
                }
            }
            break;
        case 2:
            for (int row = 0; row < size; ++row) {
                for (int column = 0; column < size; ++column) {
                    out[row * size + column] = left[row];
                }
            }
            break;
        default:
            for (int row = 0; row < size; ++row) {
                for (int column = 0; column < size; ++column) {
                    out[row * size + column] = Clip(left[row] + above[column] - corner, 0, 255);
                }
            }
            break;
    }
}

// The two passes of the inverse transform that turns a block of coefficients back into a block of
// differences. The two constants are the fixed-point forms of the rotations the transform is built
// from; the first is written as one plus its excess because the excess is what fits in sixteen bits.
void InverseDct(const int input[16], int output[16]) {
    const int cosPi8Sqrt2Minus1 = 20091;
    const int sinPi8Sqrt2 = 35468;
    int intermediate[16] = {};
    for (int index = 0; index < 4; ++index) {
        const int a1 = input[index] + input[8 + index];
        const int b1 = input[index] - input[8 + index];
        const int c1 = ((input[4 + index] * sinPi8Sqrt2) >> 16) - (input[12 + index] + ((input[12 + index] * cosPi8Sqrt2Minus1) >> 16));
        const int d1 = (input[4 + index] + ((input[4 + index] * cosPi8Sqrt2Minus1) >> 16)) + ((input[12 + index] * sinPi8Sqrt2) >> 16);
        intermediate[index] = a1 + d1;
        intermediate[12 + index] = a1 - d1;
        intermediate[4 + index] = b1 + c1;
        intermediate[8 + index] = b1 - c1;
    }
    for (int index = 0; index < 4; ++index) {
        const int base = index * 4;
        const int p0 = intermediate[base];
        const int p1 = intermediate[base + 1];
        const int p2 = intermediate[base + 2];
        const int p3 = intermediate[base + 3];
        const int a1 = p0 + p2;
        const int b1 = p0 - p2;
        const int c1 = ((p1 * sinPi8Sqrt2) >> 16) - (p3 + ((p3 * cosPi8Sqrt2Minus1) >> 16));
        const int d1 = (p1 + ((p1 * cosPi8Sqrt2Minus1) >> 16)) + ((p3 * sinPi8Sqrt2) >> 16);
        output[base] = (a1 + d1 + 4) >> 3;
        output[base + 3] = (a1 - d1 + 4) >> 3;
        output[base + 1] = (b1 + c1 + 4) >> 3;
        output[base + 2] = (b1 - c1 + 4) >> 3;
    }
}

// The inverse of the transform the sixteen coarse values of a macroblock are put through. Its answer
// is the one coefficient each of the sixteen luma blocks starts from.
void InverseWalsh(const int input[16], int output[16]) {
    int intermediate[16] = {};
    for (int index = 0; index < 4; ++index) {
        const int a1 = input[index] + input[index + 12];
        const int b1 = input[index + 4] + input[index + 8];
        const int c1 = input[index + 4] - input[index + 8];
        const int d1 = input[index] - input[index + 12];
        intermediate[index] = a1 + b1;
        intermediate[index + 4] = c1 + d1;
        intermediate[index + 8] = a1 - b1;
        intermediate[index + 12] = d1 - c1;
    }
    for (int index = 0; index < 4; ++index) {
        const int base = index * 4;
        const int a1 = intermediate[base] + intermediate[base + 3];
        const int b1 = intermediate[base + 1] + intermediate[base + 2];
        const int c1 = intermediate[base + 1] - intermediate[base + 2];
        const int d1 = intermediate[base] - intermediate[base + 3];
        output[base] = (a1 + b1 + 3) >> 3;
        output[base + 1] = (c1 + d1 + 3) >> 3;
        output[base + 2] = (a1 - b1 + 3) >> 3;
        output[base + 3] = (d1 - c1 + 3) >> 3;
    }
}

// The loop filter takes the hard edge between two blocks that were coded apart and makes it look
// like one picture, but only where the step across the edge is small enough to be an artifact of
// coding rather than something the picture really contains. Pixels are carried here as signed values
// centred on zero, which is how every figure in the filter is written.
int Saturate(int value) {
    return value < -128 ? -128 : (value < 128 ? value : 127);
}

int CommonAdjust(bool useOuterTaps, int p1, int p0, int q0, int q1, int& adjustedP0, int& adjustedQ0) {
    const int sp1 = p1 - 128;
    const int sp0 = p0 - 128;
    const int sq0 = q0 - 128;
    const int sq1 = q1 - 128;
    int a = Saturate((useOuterTaps ? Saturate(sp1 - sq1) : 0) + 3 * (sq0 - sp0));
    const int b = Saturate(a + 3) >> 3;
    a = Saturate(a + 4) >> 3;
    adjustedQ0 = Saturate(sq0 - a) + 128;
    adjustedP0 = Saturate(sp0 + b) + 128;
    return a;
}

// Whether the eight pixels straddling an edge are smooth enough for the filter to touch them at all.
int FilterAllowed(int interiorLimit, int edgeLimit, const unsigned char* before, const unsigned char* after) {
    if (Magnitude(before[3] - after[0]) * 2 + Magnitude(before[2] - after[1]) / 2 > edgeLimit) {
        return 0;
    }
    for (int index = 0; index < 3; ++index) {
        if (Magnitude(before[index] - before[index + 1]) > interiorLimit) {
            return 0;
        }
        if (Magnitude(after[index] - after[index + 1]) > interiorLimit) {
            return 0;
        }
    }
    return 1;
}

int HighEdgeVariance(int threshold, int p1, int p0, int q0, int q1) {
    return Magnitude(p1 - p0) > threshold || Magnitude(q1 - q0) > threshold;
}

// Between two blocks inside one macroblock the pixels either side of the edge are nudged toward each
// other and, when the step is a smooth one, so are the two behind them by half as much.
void SubblockFilter(int hevThreshold, int interiorLimit, int edgeLimit, unsigned char* before, unsigned char* after) {
    if (!FilterAllowed(interiorLimit, edgeLimit, before, after)) {
        return;
    }
    const int p1 = before[2];
    const int p0 = before[3];
    const int q0 = after[0];
    const int q1 = after[1];
    const bool variance = HighEdgeVariance(hevThreshold, p1, p0, q0, q1) != 0;
    int newP0 = p0;
    int newQ0 = q0;
    const int a = (CommonAdjust(variance, p1, p0, q0, q1, newP0, newQ0) + 1) >> 1;
    before[3] = static_cast<unsigned char>(newP0);
    after[0] = static_cast<unsigned char>(newQ0);
    if (!variance) {
        after[1] = static_cast<unsigned char>(Clip(q1 - a, 0, 255));
        before[2] = static_cast<unsigned char>(Clip(p1 + a, 0, 255));
    }
}

// Between two macroblocks the filter reaches further: a smooth step is spread over three pixels
// either side, each pair taking a smaller share of it the further it is from the edge.
void MacroblockFilter(int hevThreshold, int interiorLimit, int edgeLimit, unsigned char* before, unsigned char* after) {
    if (!FilterAllowed(interiorLimit, edgeLimit, before, after)) {
        return;
    }
    const int p1 = before[2];
    const int p0 = before[3];
    const int q0 = after[0];
    const int q1 = after[1];
    if (HighEdgeVariance(hevThreshold, p1, p0, q0, q1)) {
        int newP0 = p0;
        int newQ0 = q0;
        CommonAdjust(true, p1, p0, q0, q1, newP0, newQ0);
        before[3] = static_cast<unsigned char>(newP0);
        after[0] = static_cast<unsigned char>(newQ0);
        return;
    }
    const int sp1 = p1 - 128;
    const int sp0 = p0 - 128;
    const int sq0 = q0 - 128;
    const int sq1 = q1 - 128;
    const int sp2 = before[1] - 128;
    const int sq2 = after[2] - 128;
    const int weight = Saturate(Saturate(sp1 - sq1) + 3 * (sq0 - sp0));
    int a = Saturate((27 * weight + 63) >> 7);
    after[0] = static_cast<unsigned char>(Saturate(sq0 - a) + 128);
    before[3] = static_cast<unsigned char>(Saturate(sp0 + a) + 128);
    a = Saturate((18 * weight + 63) >> 7);
    after[1] = static_cast<unsigned char>(Saturate(sq1 - a) + 128);
    before[2] = static_cast<unsigned char>(Saturate(sp1 + a) + 128);
    a = Saturate((9 * weight + 63) >> 7);
    after[2] = static_cast<unsigned char>(Saturate(sq2 - a) + 128);
    before[1] = static_cast<unsigned char>(Saturate(sp2 + a) + 128);
}

// The one adjustment that is not a filter: the two pixels either side of an edge are each moved a
// quarter of the way toward the other, with the rounding arranged so their average is kept.
void SimpleFilter(int edgeLimit, unsigned char* pixels) {
    if (Magnitude(pixels[-1] - pixels[0]) * 2 + Magnitude(pixels[-2] - pixels[1]) / 2 > edgeLimit) {
        return;
    }
    int newP0 = pixels[-1];
    int newQ0 = pixels[0];
    CommonAdjust(true, pixels[-2], pixels[-1], pixels[0], pixels[1], newP0, newQ0);
    pixels[-1] = static_cast<unsigned char>(newP0);
    pixels[0] = static_cast<unsigned char>(newQ0);
}

// A whole block predicted in one of the plain ways is asked for its mode, when it becomes a
// neighbour, as though it had been a quarter block; each plain way means the quarter way that
// predicts the same thing. The plain blocks are the ones above, to the left, true motion, and the
// even average, which are modes one, two, three, and zero of a whole block in that order.
int ContextModeForWholeBlock(int wholeMode) {
    switch (wholeMode) {
        case 1:
            return 2;
        case 2:
            return 3;
        case 3:
            return 1;
        default:
            return 0;
    }
}

struct MacroBlockInfo {
    int segment = 0;
    bool skipCoefficients = false;
    bool quarterPredicted = false;
    bool hasCoefficients = false;
    int filterLevel = 0;
    int nzY2 = 0;
    int nzY[16] = {};
    int nzU[4] = {};
    int nzV[4] = {};
};

class Vp8Decoder {
public:
    bool Read(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra);

private:
    bool ReadHeader(const unsigned char* payload, int payloadSize);
    bool DecodeMacroBlocks();
    void ApplyLoopFilter();
    void FilterPlane(std::vector<unsigned char>& plane, int stride, int x, int yStart, int count, int interiorLimit, int edgeLimit,
                     int hevThreshold, bool macroEdge, bool simple);
    void FilterPlaneHorizontal(std::vector<unsigned char>& plane, int stride, int y, int xStart, int count, int interiorLimit, int edgeLimit,
                               int hevThreshold, bool macroEdge, bool simple);
    int ReadCoefficients(BoolReader& reader, int context, int first, int planeKind, int segment, int out[16]);

    int frameWidth = 0;
    int frameHeight = 0;
    int macroBlocksWide = 0;
    int macroBlocksHigh = 0;
    int yStride = 0;
    int uvStride = 0;
    int probabilities[1056] = {};
    int dcQuantizer[kNumSegments][3] = {};
    int acQuantizer[kNumSegments][3] = {};
    bool useSkipProbability = false;
    int skipProbability = 0;
    bool simpleFilter = false;
    int filterLevel = 0;
    int sharpness = 0;
    bool lfDeltaEnabled = false;
    int refLfDelta[4] = {};
    int modeLfDelta[4] = {};
    bool segmentationEnabled = false;
    bool segmentAbsolute = false;
    bool updateSegmentMap = false;
    int segmentProbabilities[3] = {255, 255, 255};
    int segmentFilter[kNumSegments] = {};
    int partitionCount = 1;
    BoolReader reader;
    std::vector<BoolReader> tokenReaders;
    std::vector<unsigned char> yPlane;
    std::vector<unsigned char> uPlane;
    std::vector<unsigned char> vPlane;
    std::vector<MacroBlockInfo> info;
    std::vector<int> aboveY;
    std::vector<int> aboveU;
    std::vector<int> aboveV;
    std::vector<int> aboveY2;
    std::vector<int> aboveModes;
};

int Vp8Decoder::ReadCoefficients(BoolReader& tokens, int context, int first, int planeKind, int segment, int out[16]) {
    // A luma block inside one that carries a coarse block already holds its own first figure, the one
    // the inverse coarse transform handed it, so only what follows is cleared here.
    for (int index = first; index < 16; ++index) {
        out[index] = 0;
    }
    // The four probability sets are luma beside a coarse block, the coarse block itself, colour,
    // and luma with no coarse block to lean on; the set for luma beside a coarse block is the one
    // whose nearest figure is never coded, because the coarse block already carries it.
    const int plane = planeKind == kCoarseDc ? 1 : (planeKind == kChromaDc ? 2 : (first == 1 ? 0 : 3));
    const int quantizer = planeKind == kCoarseDc ? 0 : (planeKind == kChromaDc ? 2 : 1);
    const int dc = dcQuantizer[segment][quantizer];
    const int ac = acQuantizer[segment][quantizer];
    int position = first;
    if (position >= 16) {
        return position;
    }
    static const int kCategoryBits[4] = {3, 4, 5, 11};
    static const int kCategoryBase[4] = {11, 19, 35, 67};
    static const int kExtras[4][11] = {{173, 148, 140, 0, 0, 0, 0, 0, 0, 0, 0},
                                       {176, 155, 140, 135, 0, 0, 0, 0, 0, 0, 0},
                                       {180, 157, 141, 134, 130, 0, 0, 0, 0, 0, 0},
                                       {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129}};
    int tableContext = context;
    // The eleven figures of a table begin with the chance of running out of block; everything else
    // sits one place further along, so the tree starts at one whether or not that chance is offered.
    // An end-of-block cannot follow a zero, so after one the tree is simply entered one branch down.
    bool noEndOfBlock = false;
    const int base = 1;
    while (position < 16) {
        const int* table = probabilities + ((plane * 8 + kBands[position]) * 3 + tableContext) * 11;
        if (!noEndOfBlock) {
            if (tokens.Read(table[0]) == 0) {
                return position;
            }
        }
        if (tokens.Read(table[base]) != 0) {
            int magnitude = 1;
            if (tokens.Read(table[base + 1]) != 0) {
                if (tokens.Read(table[base + 2]) == 0) {
                    if (tokens.Read(table[base + 3]) == 0) {
                        magnitude = 2;
                    } else {
                        magnitude = 3 + tokens.Read(table[base + 4]);
                    }
                } else if (tokens.Read(table[base + 5]) == 0) {
                    if (tokens.Read(table[base + 6]) == 0) {
                        magnitude = 5 + tokens.Read(159);
                    } else {
                        magnitude = 7 + 2 * tokens.Read(165) + tokens.Read(145);
                    }
                } else {
                    const int high = tokens.Read(table[base + 7]);
                    const int low = tokens.Read(table[base + 8 + high]);
                    const int category = 2 * high + low;
                    int extra = 0;
                    for (int bit = 0; bit < kCategoryBits[category]; ++bit) {
                        extra = (extra << 1) + tokens.Read(kExtras[category][bit]);
                    }
                    magnitude = kCategoryBase[category] + extra;
                }
            }
            if (tokens.Read(128) != 0) {
                magnitude = -magnitude;
            }
            out[kZigZag[position]] = magnitude * (position == 0 ? dc : ac);
            ++position;
            // A lone one leaves the next coefficient in the context of a one, anything larger in
            // the context of a large value.
            tableContext = Magnitude(magnitude) == 1 ? 1 : 2;
            noEndOfBlock = false;
        } else {
            out[kZigZag[position]] = 0;
            ++position;
            tableContext = 0;
            noEndOfBlock = true;
        }
    }
    return 16;
}

bool Vp8Decoder::ReadHeader(const unsigned char* payload, int payloadSize) {
    if (!reader.Open(payload, payloadSize)) {
        return false;
    }
    reader.Read(128);
    reader.Read(128);
    segmentationEnabled = reader.Read(128) != 0;
    int segmentQuantizer[kNumSegments] = {};
    if (segmentationEnabled) {
        updateSegmentMap = reader.Read(128) != 0;
        if (reader.Read(128)) {
            segmentAbsolute = reader.Read(128) != 0;
            // The quantizer deltas all come first and the filter deltas all come after them; the
            // two runs are not interleaved, and reading them any other way steps off the stream.
            for (int index = 0; index < kNumSegments; ++index) {
                segmentQuantizer[index] = reader.Read(128) ? reader.SignedLiteral(7) : 0;
            }
            for (int index = 0; index < kNumSegments; ++index) {
                segmentFilter[index] = reader.Read(128) ? reader.SignedLiteral(6) : 0;
            }
        }
        if (updateSegmentMap) {
            for (int index = 0; index < 3; ++index) {
                if (reader.Read(128)) {
                    segmentProbabilities[index] = reader.Literal(8);
                }
            }
        }
    }
    simpleFilter = reader.Read(128) != 0;
    filterLevel = reader.Literal(6);
    sharpness = reader.Literal(3);
    lfDeltaEnabled = reader.Read(128) != 0;
    if (lfDeltaEnabled && reader.Read(128)) {
        for (int index = 0; index < 4; ++index) {
            if (reader.Read(128)) {
                refLfDelta[index] = reader.SignedLiteral(6);
            }
        }
        for (int index = 0; index < 4; ++index) {
            if (reader.Read(128)) {
                modeLfDelta[index] = reader.SignedLiteral(6);
            }
        }
    }
    partitionCount = 1 << reader.Literal(2);
    const int baseQuantizer = reader.Literal(7);
    const int deltas[5] = {reader.Read(128) ? reader.SignedLiteral(4) : 0, reader.Read(128) ? reader.SignedLiteral(4) : 0,
                           reader.Read(128) ? reader.SignedLiteral(4) : 0, reader.Read(128) ? reader.SignedLiteral(4) : 0,
                           reader.Read(128) ? reader.SignedLiteral(4) : 0};
    // Every picture here is a whole one, so the coefficient probabilities start from the fixed
    // table and any of them may still be replaced before coding begins.
    reader.Read(128);
    for (int index = 0; index < 1056; ++index) {
        probabilities[index] = kCoeffProb[index];
        if (reader.Read(kCoeffUpdate[index])) {
            probabilities[index] = reader.Literal(8);
        }
    }
    useSkipProbability = reader.Read(128) != 0;
    if (useSkipProbability) {
        skipProbability = reader.Literal(8);
    }
    for (int segment = 0; segment < kNumSegments; ++segment) {
        const int base = segmentAbsolute ? Clip(segmentQuantizer[segment], 0, 127) : Clip(baseQuantizer + segmentQuantizer[segment], 0, 127);
        dcQuantizer[segment][0] = kDcQ[Clip(base + deltas[1], 0, 127)] * 2;
        acQuantizer[segment][0] = kAcQ[Clip(base + deltas[2], 0, 127)] * 155 / 100;
        if (acQuantizer[segment][0] < 8) {
            acQuantizer[segment][0] = 8;
        }
        dcQuantizer[segment][1] = kDcQ[Clip(base + deltas[0], 0, 127)];
        acQuantizer[segment][1] = kAcQ[base];
        dcQuantizer[segment][2] = kDcQ[Clip(base + deltas[3], 0, 127)];
        acQuantizer[segment][2] = kAcQ[Clip(base + deltas[4], 0, 127)];
        if (dcQuantizer[segment][2] > 132) {
            dcQuantizer[segment][2] = 132;
        }
    }
    return true;
}

// Places the row above a block, the column to its left and the pixel between them, filling in the
// values the format names for the pixels just outside the picture: plain black above it, and a
// slightly brighter grey to its left.
void GatherNeighbours(const std::vector<unsigned char>& plane, int stride, int x, int y, int width, int height, int count, int above[],
                      int left[], int& corner) {
    corner = 127;
    if (y > 0) {
        corner = x > 0 ? plane[static_cast<std::size_t>(y - 1) * stride + x - 1] : 129;
    }
    for (int index = 0; index < count; ++index) {
        if (y <= 0) {
            above[index] = 127;
        } else if (x + index < width) {
            above[index] = plane[static_cast<std::size_t>(y - 1) * stride + x + index];
        } else {
            above[index] = plane[static_cast<std::size_t>(y - 1) * stride + x - 1];
        }
        if (x <= 0) {
            left[index] = 129;
        } else if (y + index < height) {
            left[index] = plane[static_cast<std::size_t>(y + index) * stride + x - 1];
        } else {
            left[index] = plane[static_cast<std::size_t>(y - 1) * stride + x - 1];
        }
    }
}

bool Vp8Decoder::DecodeMacroBlocks() {
    const int lumaWidth = macroBlocksWide * 16;
    const int lumaHeight = macroBlocksHigh * 16;
    const int chromaWidth = macroBlocksWide * 8;
    const int chromaHeight = macroBlocksHigh * 8;
    yStride = lumaWidth;
    uvStride = chromaWidth;
    yPlane.assign(static_cast<std::size_t>(lumaWidth) * lumaHeight, 0);
    uPlane.assign(static_cast<std::size_t>(chromaWidth) * chromaHeight, 0);
    vPlane.assign(static_cast<std::size_t>(chromaWidth) * chromaHeight, 0);
    info.assign(static_cast<std::size_t>(macroBlocksWide) * macroBlocksHigh, MacroBlockInfo());
    aboveY.assign(static_cast<std::size_t>(macroBlocksWide) * 4, 0);
    aboveU.assign(static_cast<std::size_t>(macroBlocksWide) * 2, 0);
    aboveV.assign(static_cast<std::size_t>(macroBlocksWide) * 2, 0);
    aboveY2.assign(static_cast<std::size_t>(macroBlocksWide), 0);
    aboveModes.assign(static_cast<std::size_t>(macroBlocksWide) * 4, 0);
    for (int macroY = 0; macroY < macroBlocksHigh; ++macroY) {
        int leftY[4] = {};
        int leftU[2] = {};
        int leftV[2] = {};
        int leftModes[4] = {};
        int leftY2 = 0;
        BoolReader& tokens = tokenReaders[static_cast<std::size_t>(macroY) % tokenReaders.size()];
        for (int macroX = 0; macroX < macroBlocksWide; ++macroX) {
            MacroBlockInfo& block = info[static_cast<std::size_t>(macroY) * macroBlocksWide + macroX];
            if (updateSegmentMap) {
                block.segment = reader.Tree(kSegmentTree, segmentProbabilities);
                if (block.segment < 0 || block.segment >= kNumSegments) {
                    block.segment = 0;
                }
            }
            block.skipCoefficients = useSkipProbability && reader.Read(skipProbability) != 0;
            const int yMode = reader.Tree(kYModeTree, kYModeProbabilities);
            block.quarterPredicted = yMode == 4;
            int bModes[16] = {};
            if (block.quarterPredicted) {
                for (int index = 0; index < 16; ++index) {
                    const int row = index / 4;
                    const int column = index % 4;
                    const int above = row == 0 ? aboveModes[static_cast<std::size_t>(macroX) * 4 + column] : bModes[index - 4];
                    const int left = column == 0 ? leftModes[row] : bModes[index - 1];
                    bModes[index] = reader.Tree(kBmodeTree, kBmodeProb + (above * 10 + left) * 9);
                }
            }
            const int uvMode = reader.Tree(kUvModeTree, kUvModeProbabilities);
            int baseLevel = filterLevel;
            if (segmentationEnabled) {
                baseLevel = segmentAbsolute ? segmentFilter[block.segment] : filterLevel + segmentFilter[block.segment];
            }
            int level = baseLevel;
            if (lfDeltaEnabled) {
                // Every picture here is a whole one, so the reference the block leans on is always
                // the frame being drawn, which is the first of the four. Only a block that predicts
                // each quarter on its own then asks for the first of the mode adjustments; the others
                // belong to blocks that lean on an earlier or a later frame, of which there are none.
                level += refLfDelta[0];
                if (block.quarterPredicted) {
                    level += modeLfDelta[0];
                }
            }
            block.filterLevel = Clip(level, 0, 63);
            int coefficients[kMaxCoefficients] = {};
            bool anyCoefficients = false;
            if (!block.skipCoefficients) {
                if (!block.quarterPredicted) {
                    int coarse[16];
                    const int count = ReadCoefficients(tokens, aboveY2[macroX] + leftY2, 0, kCoarseDc, block.segment, coarse);
                    block.nzY2 = count > 0 ? 1 : 0;
                    if (count > 1) {
                        anyCoefficients = true;
                        int transformed[16];
                        InverseWalsh(coarse, transformed);
                        for (int index = 0; index < 16; ++index) {
                            coefficients[index * 16] = transformed[index];
                        }
                    } else {
                        const int value = (coarse[0] + 3) >> 3;
                        for (int index = 0; index < 16; ++index) {
                            coefficients[index * 16] = value;
                        }
                    }
                }
                for (int row = 0; row < 4; ++row) {
                    int left = leftY[row];
                    for (int column = 0; column < 4; ++column) {
                        const int index = row * 4 + column;
                        // Only the top row of quarters leans on the macroblock above; the rest lean on
                        // the row of quarters directly above them inside this one.
                        const int above = row == 0 ? aboveY[static_cast<std::size_t>(macroX) * 4 + column] : block.nzY[index - 4];
                        const int first = block.quarterPredicted ? 0 : 1;
                        const int count = ReadCoefficients(tokens, left + above, first, kLumaDc, block.segment, coefficients + index * 16);
                        left = count > first ? 1 : 0;
                        if (left) {
                            anyCoefficients = true;
                        }
                        block.nzY[index] = left;
                    }
                    leftY[row] = left;
                }
                for (int column = 0; column < 4; ++column) {
                    aboveY[static_cast<std::size_t>(macroX) * 4 + column] = block.nzY[12 + column];
                }
                for (int channel = 0; channel < 2; ++channel) {
                    int* abovePlane = channel == 0 ? aboveU.data() : aboveV.data();
                    int* leftPlane = channel == 0 ? leftU : leftV;
                    int* nzPlane = channel == 0 ? block.nzU : block.nzV;
                    const int base = 16 + channel * 4;
                    for (int row = 0; row < 2; ++row) {
                        int left = leftPlane[row];
                        for (int column = 0; column < 2; ++column) {
                            const int index = row * 2 + column;
                            const int above = row == 0 ? abovePlane[macroX * 2 + column] : nzPlane[index - 2];
                            const int count = ReadCoefficients(tokens, left + above, 0, kChromaDc, block.segment, coefficients + (base + index) * 16);
                            left = count > 0 ? 1 : 0;
                            if (left) {
                                anyCoefficients = true;
                            }
                            nzPlane[index] = left;
                        }
                        leftPlane[row] = left;
                    }
                    for (int column = 0; column < 2; ++column) {
                        abovePlane[macroX * 2 + column] = nzPlane[2 + column];
                    }
                }
            }
            block.hasCoefficients = anyCoefficients;
            const int originX = macroX * 16;
            const int originY = macroY * 16;
            if (block.quarterPredicted) {
                for (int index = 0; index < 16; ++index) {
                    const int row = index / 4;
                    const int column = index % 4;
                    const int blockX = originX + column * 4;
                    const int blockY = originY + row * 4;
                    int above[8];
                    int left[4];
                    int corner = 127;
                    for (int slot = 0; slot < 8; ++slot) {
                        const int px = blockX + slot;
                        if (px < originX + 16) {
                            above[slot] = blockY == 0 ? 127 : yPlane[static_cast<std::size_t>(blockY - 1) * yStride + px];
                        } else if (originY == 0) {
                            // Only the four pixels past the right edge of the rightmost quarter of a
                            // macroblock reach outside it. They lie on the line above the macroblock,
                            // which on the first macroblock row does not exist, so the format says to
                            // use the plain black the rest of that row uses.
                            above[slot] = 127;
                        } else if (px < lumaWidth) {
                            above[slot] = yPlane[static_cast<std::size_t>(originY - 1) * yStride + px];
                        } else {
                            // Past the right edge of the picture the format says to stand in the last
                            // pixel of the line above the macroblock.
                            above[slot] = yPlane[static_cast<std::size_t>(originY - 1) * yStride + originX + 15];
                        }
                    }
                    for (int slot = 0; slot < 4; ++slot) {
                        left[slot] = blockX == 0 ? 129 : yPlane[static_cast<std::size_t>(blockY + slot) * yStride + blockX - 1];
                    }
                    if (blockY == 0) {
                        corner = 127;
                    } else if (blockX == 0) {
                        corner = 129;
                    } else {
                        corner = yPlane[static_cast<std::size_t>(blockY - 1) * yStride + blockX - 1];
                    }
                    int prediction[16];
                    PredictBlock(bModes[index], above, left, corner, prediction);
                    int residue[16];
                    InverseDct(coefficients + index * 16, residue);
                    for (int slot = 0; slot < 16; ++slot) {
                        const int py = blockY + slot / 4;
                        const int px = blockX + slot % 4;
                        yPlane[static_cast<std::size_t>(py) * yStride + px] = static_cast<unsigned char>(Clip(prediction[slot] + residue[slot], 0, 255));
                    }
                }
            } else {
                int above[16];
                int left[16];
                int corner = 127;
                GatherNeighbours(yPlane, yStride, originX, originY, lumaWidth, lumaHeight, 16, above, left, corner);
                int prediction[256];
                PredictWhole(yMode, 16, originY > 0, originX > 0, above, left, corner, prediction);
                for (int index = 0; index < 16; ++index) {
                    int residue[16];
                    InverseDct(coefficients + index * 16, residue);
                    const int row = index / 4;
                    const int column = index % 4;
                    for (int slot = 0; slot < 16; ++slot) {
                        const int py = row * 4 + slot / 4;
                        const int px = column * 4 + slot % 4;
                        yPlane[static_cast<std::size_t>(originY + py) * yStride + originX + px] =
                            static_cast<unsigned char>(Clip(prediction[py * 16 + px] + residue[slot], 0, 255));
                    }
                }
            }
            const int planeX = macroX * 8;
            const int planeY = macroY * 8;
            for (int channel = 0; channel < 2; ++channel) {
                std::vector<unsigned char>& plane = channel == 0 ? uPlane : vPlane;
                int above[8];
                int left[8];
                int corner = 127;
                GatherNeighbours(plane, uvStride, planeX, planeY, chromaWidth, chromaHeight, 8, above, left, corner);
                int prediction[64];
                PredictWhole(uvMode, 8, planeY > 0, planeX > 0, above, left, corner, prediction);
                const int base = 16 + channel * 4;
                for (int index = 0; index < 4; ++index) {
                    int residue[16];
                    InverseDct(coefficients + (base + index) * 16, residue);
                    const int row = index / 2;
                    const int column = index % 2;
                    for (int slot = 0; slot < 16; ++slot) {
                        const int py = row * 4 + slot / 4;
                        const int px = column * 4 + slot % 4;
                        plane[static_cast<std::size_t>(planeY + py) * uvStride + planeX + px] =
                            static_cast<unsigned char>(Clip(prediction[py * 8 + px] + residue[slot], 0, 255));
                    }
                }
            }
            // Only a macroblock that carries a coarse block can answer for it. One that predicts each
            // quarter on its own has none, so the answer stays with the most recent macroblock above
            // or to the left that did have one.
            if (!block.quarterPredicted) {
                aboveY2[macroX] = block.nzY2;
                leftY2 = block.nzY2;
            }
            for (int index = 0; index < 4; ++index) {
                const int contextMode = ContextModeForWholeBlock(yMode);
                aboveModes[static_cast<std::size_t>(macroX) * 4 + index] = block.quarterPredicted ? bModes[12 + index] : contextMode;
                leftModes[index] = block.quarterPredicted ? bModes[index * 4 + 3] : contextMode;
            }
        }
    }
    return true;
}

void Vp8Decoder::FilterPlane(std::vector<unsigned char>& plane, int stride, int x, int yStart, int count, int interiorLimit, int edgeLimit,
                             int hevThreshold, bool macroEdge, bool simple) {
    if (x < 4) {
        return;
    }
    for (int offset = 0; offset < count; ++offset) {
        unsigned char* before = plane.data() + static_cast<std::size_t>(yStart + offset) * stride + x - 4;
        unsigned char* after = plane.data() + static_cast<std::size_t>(yStart + offset) * stride + x;
        if (simple) {
            SimpleFilter(edgeLimit, after);
        } else if (macroEdge) {
            MacroblockFilter(hevThreshold, interiorLimit, edgeLimit, before, after);
        } else {
            SubblockFilter(hevThreshold, interiorLimit, edgeLimit, before, after);
        }
    }
}

void Vp8Decoder::FilterPlaneHorizontal(std::vector<unsigned char>& plane, int stride, int y, int xStart, int count, int interiorLimit,
                                       int edgeLimit, int hevThreshold, bool macroEdge, bool simple) {
    if (y < 4) {
        return;
    }
    for (int offset = 0; offset < count; ++offset) {
        unsigned char* before = plane.data() + static_cast<std::size_t>(y - 4) * stride + xStart + offset;
        unsigned char* after = plane.data() + static_cast<std::size_t>(y) * stride + xStart + offset;
        // A horizontal edge has its before and after pixels in different rows, so the eight of them
        // are gathered into one little run and the answer written back out again.
        unsigned char run[8];
        for (int index = 0; index < 4; ++index) {
            run[index] = before[static_cast<std::size_t>(index) * stride];
            run[4 + index] = after[static_cast<std::size_t>(index) * stride];
        }
        if (simple) {
            SimpleFilter(edgeLimit, run + 4);
        } else if (macroEdge) {
            MacroblockFilter(hevThreshold, interiorLimit, edgeLimit, run, run + 4);
        } else {
            SubblockFilter(hevThreshold, interiorLimit, edgeLimit, run, run + 4);
        }
        for (int index = 0; index < 4; ++index) {
            before[static_cast<std::size_t>(index) * stride] = run[index];
            after[static_cast<std::size_t>(index) * stride] = run[4 + index];
        }
    }
}

// The loop filter runs over the whole picture once every macroblock has been laid down, never during
// the laying down, because a prediction reads its neighbours as they were before any filtering.
void Vp8Decoder::ApplyLoopFilter() {
    for (int macroY = 0; macroY < macroBlocksHigh; ++macroY) {
        for (int macroX = 0; macroX < macroBlocksWide; ++macroX) {
            const MacroBlockInfo& block = info[static_cast<std::size_t>(macroY) * macroBlocksWide + macroX];
            const int level = block.filterLevel;
            if (level == 0) {
                continue;
            }
            int interior = level;
            if (sharpness) {
                interior >>= sharpness > 4 ? 2 : 1;
                if (interior > 9 - sharpness) {
                    interior = 9 - sharpness;
                }
            }
            if (!interior) {
                interior = 1;
            }
            // How far apart two pixels either side of an edge may be before the edge is taken to be
            // something the picture really contains rather than an artifact of coding. The plain
            // filter, which reaches across every edge it meets, uses the same two figures.
            const int macroLimit = (level + 2) * 2 + interior;
            const int subLimit = level * 2 + interior;
            int hevThreshold = 0;
            if (level >= 40) {
                hevThreshold = 2;
            } else if (level >= 15) {
                hevThreshold = 1;
            }
            const bool filterSub = block.hasCoefficients || block.quarterPredicted;
            const int originX = macroX * 16;
            const int originY = macroY * 16;
            const int planeX = macroX * 8;
            const int planeY = macroY * 8;
            if (!simpleFilter) {
                if (macroX > 0) {
                    FilterPlane(yPlane, yStride, originX, originY, 16, interior, macroLimit, hevThreshold, true, false);
                    FilterPlane(uPlane, uvStride, planeX, planeY, 8, interior, macroLimit, hevThreshold, true, false);
                    FilterPlane(vPlane, uvStride, planeX, planeY, 8, interior, macroLimit, hevThreshold, true, false);
                }
                if (filterSub) {
                    for (int step = 1; step <= 3; ++step) {
                        FilterPlane(yPlane, yStride, originX + step * 4, originY, 16, interior, subLimit, hevThreshold, false, false);
                    }
                    FilterPlane(uPlane, uvStride, planeX + 4, planeY, 8, interior, subLimit, hevThreshold, false, false);
                    FilterPlane(vPlane, uvStride, planeX + 4, planeY, 8, interior, subLimit, hevThreshold, false, false);
                }
                if (macroY > 0) {
                    FilterPlaneHorizontal(yPlane, yStride, originY, originX, 16, interior, macroLimit, hevThreshold, true, false);
                    FilterPlaneHorizontal(uPlane, uvStride, planeY, planeX, 8, interior, macroLimit, hevThreshold, true, false);
                    FilterPlaneHorizontal(vPlane, uvStride, planeY, planeX, 8, interior, macroLimit, hevThreshold, true, false);
                }
                if (filterSub) {
                    for (int step = 1; step <= 3; ++step) {
                        FilterPlaneHorizontal(yPlane, yStride, originY + step * 4, originX, 16, interior, subLimit, hevThreshold, false, false);
                    }
                    FilterPlaneHorizontal(uPlane, uvStride, planeY + 4, planeX, 8, interior, subLimit, hevThreshold, false, false);
                    FilterPlaneHorizontal(vPlane, uvStride, planeY + 4, planeX, 8, interior, subLimit, hevThreshold, false, false);
                }
            } else {
                // The simple filter only ever touches luma, and reaches across every edge it meets.
                if (macroX > 0) {
                    FilterPlane(yPlane, yStride, originX, originY, 16, interior, macroLimit, hevThreshold, true, true);
                }
                if (filterSub) {
                    for (int step = 1; step <= 3; ++step) {
                        FilterPlane(yPlane, yStride, originX + step * 4, originY, 16, interior, subLimit, hevThreshold, false, true);
                    }
                }
                if (macroY > 0) {
                    FilterPlaneHorizontal(yPlane, yStride, originY, originX, 16, interior, macroLimit, hevThreshold, true, true);
                }
                if (filterSub) {
                    for (int step = 1; step <= 3; ++step) {
                        FilterPlaneHorizontal(yPlane, yStride, originY + step * 4, originX, 16, interior, subLimit, hevThreshold, false, true);
                    }
                }
            }
        }
    }
}

bool Vp8Decoder::Read(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra) {
    if (data == nullptr || size < 10) {
        return false;
    }
    const std::uint32_t tag = static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) | (static_cast<std::uint32_t>(data[2]) << 16);
    // Every lossy picture inside this container is a whole one; a picture that leans on an earlier
    // one could not be drawn from the bytes here at all.
    if ((tag & 1u) != 0) {
        return false;
    }
    if (data[3] != 0x9d || data[4] != 0x01 || data[5] != 0x2a) {
        return false;
    }
    const int firstPartitionSize = static_cast<int>((tag >> 5) & 0x7FFFFu);
    frameWidth = (data[6] | (data[7] << 8)) & 0x3FFF;
    frameHeight = (data[8] | (data[9] << 8)) & 0x3FFF;
    width = frameWidth;
    height = frameHeight;
    if (frameWidth <= 0 || frameHeight <= 0 || frameWidth > kMaxEdge || frameHeight > kMaxEdge) {
        return false;
    }
    if (10 + firstPartitionSize > size) {
        return false;
    }
    if (!ReadHeader(data + 10, firstPartitionSize)) {
        return false;
    }
    int offset = 10 + firstPartitionSize;
    tokenReaders.assign(static_cast<std::size_t>(partitionCount), BoolReader());
    for (int part = 0; part < partitionCount; ++part) {
        int partSize;
        if (part == partitionCount - 1) {
            partSize = size - offset;
        } else {
            if (offset + 3 > size) {
                return false;
            }
            partSize = data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16);
            offset += 3;
        }
        if (partSize < 0 || offset + partSize > size) {
            return false;
        }
        if (!tokenReaders[static_cast<std::size_t>(part)].Open(data + offset, partSize)) {
            return false;
        }
        offset += partSize;
    }
    macroBlocksWide = (frameWidth + 15) / 16;
    macroBlocksHigh = (frameHeight + 15) / 16;
    if (!DecodeMacroBlocks()) {
        return false;
    }
    ApplyLoopFilter();
    bgra.assign(static_cast<std::size_t>(frameWidth) * frameHeight * 4, 255);
    for (int y = 0; y < frameHeight; ++y) {
        const unsigned char* yRow = yPlane.data() + static_cast<std::size_t>(y) * yStride;
        // Colour is stored at half the width and height of the picture, so each of its samples has
        // to be spread over a little square of four pixels. The sample nearest a pixel is taken at
        // three times the weight of the one beside it, in the direction towards whichever sample
        // pair the pixel falls between; a sample one place along carries a quarter of it.
        const int nearRow = y >> 1;
        const int farRow = ClampPlaneIndex((y & 1) ? nearRow + 1 : nearRow - 1, (frameHeight + 1) / 2);
        const unsigned char* uNear = uPlane.data() + static_cast<std::size_t>(nearRow) * uvStride;
        const unsigned char* uFar = uPlane.data() + static_cast<std::size_t>(farRow) * uvStride;
        const unsigned char* vNear = vPlane.data() + static_cast<std::size_t>(nearRow) * uvStride;
        const unsigned char* vFar = vPlane.data() + static_cast<std::size_t>(farRow) * uvStride;
        unsigned char* out = bgra.data() + static_cast<std::size_t>(y) * frameWidth * 4;
        for (int x = 0; x < frameWidth; ++x) {
            const int nearColumn = x >> 1;
            const int farColumn = ClampPlaneIndex((x & 1) ? nearColumn + 1 : nearColumn - 1, (frameWidth + 1) / 2);
            const int luma = yRow[x];
            const int u = SpreadChromaSample(uNear[nearColumn], uNear[farColumn], uFar[nearColumn], uFar[farColumn]);
            const int v = SpreadChromaSample(vNear[nearColumn], vNear[farColumn], vFar[nearColumn], vFar[farColumn]);
            // The colour conversion the format itself names: each channel is a fixed-point weighted
            // sum of the luma and the two colour samples, with eight of the sixteen fractional bits
            // shed before the sums are combined so the arithmetic matches the format figure for figure.
            const int lumaTerm = (19077 * luma) >> 8;
            const int red = Clip((lumaTerm + ((26149 * v) >> 8) - 14234) >> 6, 0, 255);
            const int green = Clip((lumaTerm - ((6419 * u) >> 8) - ((13320 * v) >> 8) + 8708) >> 6, 0, 255);
            const int blue = Clip((lumaTerm + ((33050 * u) >> 8) - 17685) >> 6, 0, 255);
            out[x * 4 + 0] = static_cast<unsigned char>(blue);
            out[x * 4 + 1] = static_cast<unsigned char>(green);
            out[x * 4 + 2] = static_cast<unsigned char>(red);
            out[x * 4 + 3] = 255;
        }
    }
    return true;
}

}  // namespace

bool DecodeVp8Frame(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra) {
    Vp8Decoder decoder;
    return decoder.Read(data, size, width, height, bgra);
}

}  // namespace Runtime
