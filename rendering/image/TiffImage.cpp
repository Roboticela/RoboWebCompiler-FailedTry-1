#include "rendering/image/ImageCodec.h"

#include "rendering/image/Inflate.h"

#include <cstring>
#include <vector>

namespace Runtime {
namespace {

// A tagged picture: a list of named figures, each with a type and a count, and the pixels somewhere
// else in the file that the figures point at. The list is read first, because nothing about the
// pixels can be made sense of until it is known how wide they are, how many bits a sample takes,
// whether the components are layered beside each other or one plane after another, and how the run
// of bytes that holds them was squeezed.
//
// Two orders of bytes are allowed, named by the first two bytes of the file, and every figure is read
// in the order the file names. A figure worth more than four bytes is not held in the list itself but
// at an offset, and a list of figures can be followed to a second list, which is how a picture keeps
// more than one image in one file; the first image is the one read here.

const int kMaxEdge = 16384;
const int kMaxEntries = 64;
const int kMaxCodes = 4096;

struct TiffEntry {
    int tag = 0;
    int type = 0;
    unsigned int count = 0;
    unsigned int valueOffset = 0;
    // The four bytes of the figure's value field exactly as they lie in the file, which is how a
    // figure small enough to be held there is read whatever order the file writes its numbers in.
    unsigned char valueBytes[4] = {};
};

struct TiffDirectory {
    TiffEntry entries[kMaxEntries] = {};
    int count = 0;
};

// How many bytes one value of a figure's type takes. Types the runtime does not read answer nought,
// which keeps them out of every later calculation.
int TypeSize(int type) {
    switch (type) {
        case 1:
        case 2:
        case 6:
        case 7:
            return 1;
        case 3:
        case 8:
            return 2;
        case 4:
        case 9:
        case 11:
            return 4;
        case 5:
        case 10:
        case 12:
            return 8;
        default:
            return 0;
    }
}

unsigned int ReadLe16(const unsigned char* data) {
    return static_cast<unsigned int>(data[0]) | (static_cast<unsigned int>(data[1]) << 8);
}

unsigned int ReadLe32(const unsigned char* data) {
    return static_cast<unsigned int>(data[0]) | (static_cast<unsigned int>(data[1]) << 8) | (static_cast<unsigned int>(data[2]) << 16) |
           (static_cast<unsigned int>(data[3]) << 24);
}

unsigned int ReadBe16(const unsigned char* data) {
    return (static_cast<unsigned int>(data[0]) << 8) | static_cast<unsigned int>(data[1]);
}

unsigned int ReadBe32(const unsigned char* data) {
    return (static_cast<unsigned int>(data[0]) << 24) | (static_cast<unsigned int>(data[1]) << 16) |
           (static_cast<unsigned int>(data[2]) << 8) | static_cast<unsigned int>(data[3]);
}

unsigned int ReadShort(const unsigned char* data, bool bigEndian) {
    return bigEndian ? ReadBe16(data) : ReadLe16(data);
}

unsigned int ReadLong(const unsigned char* data, bool bigEndian) {
    return bigEndian ? ReadBe32(data) : ReadLe32(data);
}

const TiffEntry* FindEntry(const TiffDirectory& directory, int tag) {
    for (int index = 0; index < directory.count; ++index) {
        if (directory.entries[index].tag == tag) {
            return &directory.entries[index];
        }
    }
    return nullptr;
}

// One figure of the list. A figure small enough to sit inside its own entry is there; a larger one
// lies at the offset the entry names, and every value of it is stepped to from there.
bool ReadEntryValue(const TiffEntry& entry, const unsigned char* data, int size, bool bigEndian, unsigned int index, unsigned int& value) {
    const int typeSize = TypeSize(entry.type);
    if (typeSize == 0 || index >= entry.count) {
        return false;
    }
    const long long byteCount = static_cast<long long>(entry.count) * typeSize;
    const unsigned char* base = nullptr;
    if (byteCount <= 4) {
        // A figure small enough to sit inside its own entry lies in those four bytes exactly as they
        // were written in the file, so they are read where they stand rather than through a number.
        base = entry.valueBytes;
    } else {
        if (entry.valueOffset < 0 || static_cast<long long>(entry.valueOffset) + byteCount > size) {
            return false;
        }
        base = data + entry.valueOffset;
    }
    const unsigned char* at = base + static_cast<long long>(index) * typeSize;
    switch (typeSize) {
        case 1:
            value = at[0];
            return true;
        case 2:
            value = ReadShort(at, bigEndian);
            return true;
        case 4:
            value = ReadLong(at, bigEndian);
            return true;
        default:
            return false;
    }
}

unsigned int EntryValue(const TiffEntry* entry, const unsigned char* data, int size, bool bigEndian, unsigned int fallback) {
    if (entry == nullptr) {
        return fallback;
    }
    unsigned int value = 0;
    if (!ReadEntryValue(*entry, data, size, bigEndian, 0, value)) {
        return fallback;
    }
    return value;
}

// Reads the list of figures that starts at an offset in the file.
bool ReadDirectory(const unsigned char* data, int size, bool bigEndian, unsigned int offset, TiffDirectory& directory) {
    if (offset + 2 > static_cast<unsigned int>(size)) {
        return false;
    }
    const unsigned int entryCount = ReadShort(data + offset, bigEndian);
    const long long first = static_cast<long long>(offset) + 2;
    if (first + static_cast<long long>(entryCount) * 12 > size) {
        return false;
    }
    directory.count = 0;
    for (unsigned int index = 0; index < entryCount && directory.count < kMaxEntries; ++index) {
        const unsigned char* at = data + first + static_cast<long long>(index) * 12;
        TiffEntry& entry = directory.entries[directory.count];
        entry.tag = static_cast<int>(ReadShort(at, bigEndian));
        entry.type = static_cast<int>(ReadShort(at + 2, bigEndian));
        entry.count = ReadLong(at + 4, bigEndian);
        entry.valueOffset = ReadLong(at + 8, bigEndian);
        std::memcpy(entry.valueBytes, at + 8, 4);
        ++directory.count;
    }
    return true;
}

// The run of bytes one row of a picture or a tile covers, padded out to the next whole byte.
long long RowBytes(long long width, int samplesPerPixel, int bitsPerSample) {
    return (width * samplesPerPixel * bitsPerSample + 7) / 8;
}

// The squeeze the bytes of a picture were put through. A run of bytes that repeats is written once
// with a count, and a run that does not is written out as it stands, each count a signed byte that
// says which of the two it is.
bool DecodePackBits(const unsigned char* data, int size, std::vector<unsigned char>& out, long long wanted) {
    out.clear();
    int at = 0;
    while (at < size && static_cast<long long>(out.size()) < wanted) {
        const signed char count = static_cast<signed char>(data[at]);
        ++at;
        if (count >= 0) {
            const int run = static_cast<int>(count) + 1;
            if (at + run > size) {
                return false;
            }
            for (int index = 0; index < run && static_cast<long long>(out.size()) < wanted; ++index) {
                out.push_back(data[at + index]);
            }
            at += run;
        } else if (count != -128) {
            if (at >= size) {
                return false;
            }
            const int run = 1 - static_cast<int>(count);
            for (int index = 0; index < run && static_cast<long long>(out.size()) < wanted; ++index) {
                out.push_back(data[at]);
            }
            ++at;
        }
    }
    return static_cast<long long>(out.size()) >= wanted;
}

// The squeeze a picture's bytes were put through by naming runs of them with codes that are handed
// out as they are met, so a run that has been seen once can be named by a single code afterwards.
// A code is nine bits long to begin with and grows a bit at a time as the names run out; the two
// smallest names are kept for starting over and for the end, so the first name handed out is two
// past the plainest one. The names grow one code sooner than the plainest reading of the rules would
// put them, which is a quirk of this format and is what every writer and reader agrees on.
bool DecodeLzw(const unsigned char* data, int size, std::vector<unsigned char>& out, long long wanted) {
    out.clear();
    std::vector<int> prefix(kMaxCodes, 0);
    std::vector<unsigned char> suffix(kMaxCodes, 0);
    std::vector<unsigned char> stack(kMaxCodes, 0);
    for (int index = 0; index < 256; ++index) {
        suffix[static_cast<std::size_t>(index)] = static_cast<unsigned char>(index);
    }
    const int clearCode = 256;
    const int endCode = 257;
    int codeSize = 9;
    int next = 258;
    int previous = -1;
    unsigned int bits = 0;
    int bitCount = 0;
    long long at = 0;
    while (static_cast<long long>(out.size()) < wanted) {
        while (bitCount < codeSize && at < size) {
            // The bits of this format run from the top of a byte downwards.
            bits = (bits << 8) | data[at];
            bitCount += 8;
            ++at;
        }
        if (bitCount < codeSize) {
            break;
        }
        bitCount -= codeSize;
        const int code = static_cast<int>((bits >> bitCount) & ((1u << codeSize) - 1u));
        if (code == clearCode) {
            codeSize = 9;
            next = 258;
            previous = -1;
            continue;
        }
        if (code == endCode) {
            break;
        }
        int current = code;
        int depth = 0;
        if (previous >= 0 && current >= next) {
            // The run named by a code that has not been handed out yet is the run of the code before
            // it with its own first byte on the end, which is the only case a reader cannot look up.
            if (current != next) {
                return false;
            }
            stack[static_cast<std::size_t>(depth++)] = suffix[static_cast<std::size_t>(previous)];
            current = previous;
        }
        while (current >= 256) {
            if (current >= next || depth >= kMaxCodes) {
                return false;
            }
            stack[static_cast<std::size_t>(depth++)] = suffix[static_cast<std::size_t>(current)];
            current = prefix[static_cast<std::size_t>(current)];
        }
        stack[static_cast<std::size_t>(depth++)] = suffix[static_cast<std::size_t>(current)];
        for (int index = depth - 1; index >= 0 && static_cast<long long>(out.size()) < wanted; --index) {
            out.push_back(stack[static_cast<std::size_t>(index)]);
        }
        if (previous >= 0 && next < kMaxCodes) {
            prefix[static_cast<std::size_t>(next)] = previous;
            suffix[static_cast<std::size_t>(next)] = suffix[static_cast<std::size_t>(current)];
            ++next;
        }
        previous = code;
        const int limit = (next + 1) < kMaxCodes ? (next + 1) : kMaxCodes;
        codeSize = limit < 512 ? 9 : (limit < 1024 ? 10 : (limit < 2048 ? 11 : 12));
    }
    return static_cast<long long>(out.size()) >= wanted;
}

// Turns the bytes of a run of a picture back into the samples they stand for, whatever squeeze they
// were put through. The answer is the samples as they were written, in the file's own order.
bool Decompress(const unsigned char* data, int size, int compression, std::vector<unsigned char>& out, long long wanted) {
    if (wanted > 0 && static_cast<long long>(size) >= wanted && compression == 1) {
        out.assign(data, data + wanted);
        return true;
    }
    switch (compression) {
        case 1:
            out.assign(data, data + (size < wanted ? size : static_cast<int>(wanted)));
            return static_cast<long long>(out.size()) >= wanted;
        case 5:
            return DecodeLzw(data, size, out, wanted);
        case 32773:
            return DecodePackBits(data, size, out, wanted);
        case 8:
            // The squeeze named here wraps the plain one in a checksum, and some writers of it leave
            // the wrapper off, so it is read either way.
            if (InflateZlib(data, size, out) && static_cast<long long>(out.size()) >= wanted) {
                return true;
            }
            return InflateRaw(data, size, out) && static_cast<long long>(out.size()) >= wanted;
        case 32946:
            if (InflateRaw(data, size, out) && static_cast<long long>(out.size()) >= wanted) {
                return true;
            }
            return InflateZlib(data, size, out) && static_cast<long long>(out.size()) >= wanted;
        default:
            return false;
    }
}

// Samples laid out one after another are stored as the difference from the sample a row above them,
// which makes a picture of smooth tones hold far smaller figures. Putting them back is a running sum
// along each row, one sample at a time, in the width the file writes them in.
void UndoPrediction(std::vector<unsigned char>& samples, long long rowBytes, int samplesPerPixel, int bitsPerSample, bool bigEndian) {
    if (bitsPerSample == 8) {
        for (long long at = samplesPerPixel; at < rowBytes; ++at) {
            samples[static_cast<std::size_t>(at)] = static_cast<unsigned char>(samples[static_cast<std::size_t>(at)] +
                                                                            samples[static_cast<std::size_t>(at - samplesPerPixel)]);
        }
        return;
    }
    if (bitsPerSample == 16) {
        for (long long at = samplesPerPixel; at * 2 + 1 < rowBytes; ++at) {
            unsigned char* current = samples.data() + at * 2;
            const unsigned char* earlier = samples.data() + (at - samplesPerPixel) * 2;
            const unsigned int a = ReadShort(current, bigEndian);
            const unsigned int b = ReadShort(earlier, bigEndian);
            const unsigned int sum = (a + b) & 0xffffu;
            if (bigEndian) {
                current[0] = static_cast<unsigned char>(sum >> 8);
                current[1] = static_cast<unsigned char>(sum & 0xff);
            } else {
                current[0] = static_cast<unsigned char>(sum & 0xff);
                current[1] = static_cast<unsigned char>(sum >> 8);
            }
        }
    }
}

// A sample narrower than a byte names one of a few evenly spaced steps rather than a tone, and the
// steps of the reading the runtime hands on are 256 of them, so a short sample is stretched to the
// full range. A sample of eight bits or more is already a tone and is left as it stands.
int ScaleSample(int value, int fullScale) {
    return fullScale == 255 ? value : value * 255 / fullScale;
}

// One sample of a pixel, taken from the place in a row the bits of the picture put it.
int ReadSample(const unsigned char* row, long long sampleIndex, int bitsPerSample, bool bigEndian) {
    switch (bitsPerSample) {
        case 1:
            return (row[sampleIndex / 8] >> (7 - (sampleIndex % 8))) & 1;
        case 4:
            return (row[sampleIndex / 2] >> ((sampleIndex % 2) == 0 ? 4 : 0)) & 15;
        case 8:
            return row[sampleIndex];
        case 16:
            // Sixteen bits of a channel are handed on as eight by keeping the top half, which is the
            // reading the format's own library takes and which hands a channel written from eight
            // bits, written as that channel times 257, straight back.
            return static_cast<int>(ReadShort(row + sampleIndex * 2, bigEndian) >> 8);
        default:
            return 0;
    }
}

// What a picture is worth is one part of it, and what every part of it is worth. A picture of a few
// bits a sample names its colours in a table; one of many bits names them itself.
struct TiffPalette {
    std::vector<unsigned char> red;
    std::vector<unsigned char> green;
    std::vector<unsigned char> blue;
};

}  // namespace

bool DecodeTiffImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 8) {
        return false;
    }
    const bool littleEndian = data[0] == 'I' && data[1] == 'I';
    const bool bigEndian = data[0] == 'M' && data[1] == 'M';
    if (!littleEndian && !bigEndian) {
        return false;
    }
    if (ReadShort(data + 2, bigEndian) != 42) {
        // The format has a second reading of its own framing that names its offsets in eight bytes
        // instead of four. No picture is written that way here, so the file is passed on.
        return false;
    }
    const unsigned int firstDirectory = ReadLong(data + 4, bigEndian);
    TiffDirectory directory;
    if (!ReadDirectory(data, size, bigEndian, firstDirectory, directory)) {
        return false;
    }

    const unsigned int width = EntryValue(FindEntry(directory, 256), data, size, bigEndian, 0);
    const unsigned int height = EntryValue(FindEntry(directory, 257), data, size, bigEndian, 0);
    const unsigned int bitsPerSample = EntryValue(FindEntry(directory, 258), data, size, bigEndian, 1);
    const unsigned int compression = EntryValue(FindEntry(directory, 259), data, size, bigEndian, 1);
    const unsigned int planarity = EntryValue(FindEntry(directory, 284), data, size, bigEndian, 1);
    const unsigned int predictor = EntryValue(FindEntry(directory, 317), data, size, bigEndian, 1);
    const unsigned int sampleFormat = EntryValue(FindEntry(directory, 339), data, size, bigEndian, 1);
    // How a picture's samples are to be read, and how many of them a pixel holds, are two halves of
    // the same figure and a file may leave either of them out, so each is worked out from the other
    // when it is missing. Three samples beside each other and no word on the matter means the three
    // are the colours the format names plainly; anything else is one sample a pixel.
    const bool hasPhotometric = FindEntry(directory, 262) != nullptr;
    const bool samplesStated = FindEntry(directory, 277) != nullptr;
    const unsigned int statedSamples = EntryValue(FindEntry(directory, 277), data, size, bigEndian, 0);
    const unsigned int photometric = EntryValue(FindEntry(directory, 262), data, size, bigEndian,
                                                 samplesStated && statedSamples >= 3 ? 2u : 1u);
    const unsigned int samplesPerPixel = samplesStated ? statedSamples : (photometric == 2 ? 3u : 1u);

    if (width < 1 || height < 1 || width > static_cast<unsigned int>(kMaxEdge) || height > static_cast<unsigned int>(kMaxEdge)) {
        return false;
    }
    if (samplesPerPixel < 1 || samplesPerPixel > 4) {
        return false;
    }
    if (bitsPerSample != 1 && bitsPerSample != 4 && bitsPerSample != 8 && bitsPerSample != 16) {
        return false;
    }
    if (planarity != 1) {
        // A picture whose colour planes are laid down one after another rather than beside each
        // other is a second way of writing the same thing; the pictures that arrive are written the
        // first way, so this one is passed on.
        return false;
    }
    if (sampleFormat != 1 && sampleFormat != 4) {
        return false;
    }
    if (predictor != 1 && predictor != 2) {
        return false;
    }
    if (photometric != 0 && photometric != 1 && photometric != 2 && photometric != 3) {
        return false;
    }
    if (photometric == 3 && bitsPerSample > 8) {
        return false;
    }
    const bool paletted = photometric == 3;
    const bool colourBySample = photometric == 2;
    const int colourSamples = colourBySample ? 3 : 1;
    if (static_cast<int>(samplesPerPixel) < colourSamples) {
        return false;
    }

    TiffPalette palette;
    if (paletted) {
        const TiffEntry* map = FindEntry(directory, 320);
        if (map == nullptr || map->count != 3u * (1u << bitsPerSample)) {
            return false;
        }
        const unsigned int entries = 1u << bitsPerSample;
        palette.red.assign(entries, 0);
        palette.green.assign(entries, 0);
        palette.blue.assign(entries, 0);
        unsigned int value = 0;
        for (unsigned int index = 0; index < entries; ++index) {
            // The table names each colour as a figure of sixteen bits, so it is folded onto the eight
            // a channel is handed on in by keeping the top half, exactly as a deep picture's own
            // samples are, so a table written from eight bits comes back without a step of drift.
            if (ReadEntryValue(*map, data, size, bigEndian, index, value)) {
                palette.red[index] = static_cast<unsigned char>(value >> 8);
            }
            if (ReadEntryValue(*map, data, size, bigEndian, entries + index, value)) {
                palette.green[index] = static_cast<unsigned char>(value >> 8);
            }
            if (ReadEntryValue(*map, data, size, bigEndian, 2 * entries + index, value)) {
                palette.blue[index] = static_cast<unsigned char>(value >> 8);
            }
        }
    }

    // A picture of a few bits a sample that is colour by number keeps its colours in a table; one of
    // one bit that says nothing about which end is black is written with noughts for white, which is
    // the reading the plainest pictures of the format use.
    const bool inverted = (photometric == 0) || (!colourBySample && !paletted && !hasPhotometric && bitsPerSample == 1);
    const bool hasAlpha = (photometric == 3 || colourBySample) ? (static_cast<int>(samplesPerPixel) > colourSamples) : (samplesPerPixel >= 2);
    const unsigned int extraSamples = EntryValue(FindEntry(directory, 338), data, size, bigEndian, 0);
    // The largest figure a sample of this picture can hold, which is the step a tone is stretched
    // from. Bits beyond the eighth have already been folded onto a byte by the sample reader.
    const int fullScale = bitsPerSample >= 8 ? 255 : ((1 << bitsPerSample) - 1);

    // The picture is written in strips that run the width of it, or in tiles that cover it in a grid;
    // either way the runs of bytes are read one after another and the pixels they hold are placed.
    const TiffEntry* stripOffsets = FindEntry(directory, 273);
    const TiffEntry* stripByteCounts = FindEntry(directory, 279);
    const TiffEntry* tileOffsets = FindEntry(directory, 324);
    const TiffEntry* tileByteCounts = FindEntry(directory, 325);
    const bool tiled = tileOffsets != nullptr;
    if (!tiled && stripOffsets == nullptr) {
        return false;
    }
    const unsigned int rowsPerStrip = tiled ? 0 : EntryValue(FindEntry(directory, 278), data, size, bigEndian, height);
    const unsigned int tileWidth = tiled ? EntryValue(FindEntry(directory, 322), data, size, bigEndian, 0) : width;
    const unsigned int tileLength = tiled ? EntryValue(FindEntry(directory, 323), data, size, bigEndian, 0) : 0;
    if (tiled && (tileWidth < 1 || tileLength < 1 || tileWidth > static_cast<unsigned int>(kMaxEdge) || tileLength > static_cast<unsigned int>(kMaxEdge))) {
        return false;
    }
    if (!tiled && rowsPerStrip < 1) {
        return false;
    }

    ImageFrame frame;
    frame.width = static_cast<int>(width);
    frame.height = static_cast<int>(height);
    frame.bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    frame.premultiplied = hasAlpha && extraSamples == 1;
    bool sawAlpha = false;

    const unsigned int across = tiled ? (width + tileWidth - 1) / tileWidth : 1;
    const unsigned int down = tiled ? (height + tileLength - 1) / tileLength : (height + rowsPerStrip - 1) / rowsPerStrip;
    const unsigned int runCount = across * down;
    const TiffEntry* offsets = tiled ? tileOffsets : stripOffsets;
    const TiffEntry* lengths = tiled ? tileByteCounts : stripByteCounts;
    if (offsets->count < runCount) {
        return false;
    }
    std::vector<unsigned char> samples;
    std::vector<unsigned char> decoded;
    for (unsigned int run = 0; run < runCount; ++run) {
        unsigned int offset = 0;
        if (!ReadEntryValue(*offsets, data, size, bigEndian, run, offset)) {
            return false;
        }
        unsigned int byteCount = 0;
        if (lengths != nullptr) {
            if (!ReadEntryValue(*lengths, data, size, bigEndian, run, byteCount)) {
                return false;
            }
        } else {
            byteCount = static_cast<unsigned int>(size) > offset ? static_cast<unsigned int>(size) - offset : 0;
        }
        if (offset >= static_cast<unsigned int>(size) || byteCount == 0 || offset + byteCount > static_cast<unsigned int>(size)) {
            // A run of bytes that is not there is a picture that cannot be drawn in full, and half a
            // picture laid over another is worse than none at all.
            return false;
        }
        const unsigned int runWidth = tiled ? tileWidth : width;
        const unsigned int runHeight = tiled ? tileLength : (rowsPerStrip < height - run * rowsPerStrip ? rowsPerStrip : height - run * rowsPerStrip);
        const int across0 = tiled ? static_cast<int>(run % across) * static_cast<int>(tileWidth) : 0;
        const int down0 = tiled ? static_cast<int>(run / across) * static_cast<int>(tileLength) : static_cast<int>(run * rowsPerStrip);
        const long long rowBytes = RowBytes(runWidth, static_cast<int>(samplesPerPixel), static_cast<int>(bitsPerSample));
        const long long wanted = rowBytes * runHeight;
        if (!Decompress(data + offset, static_cast<int>(byteCount), static_cast<int>(compression), decoded, wanted)) {
            return false;
        }
        for (unsigned int line = 0; line < runHeight; ++line) {
            const int y = down0 + static_cast<int>(line);
            if (y >= static_cast<int>(height)) {
                break;
            }
            samples.assign(decoded.begin() + static_cast<long long>(line) * rowBytes,
                           decoded.begin() + static_cast<long long>(line) * rowBytes + rowBytes);
            if (predictor == 2) {
                UndoPrediction(samples, rowBytes, static_cast<int>(samplesPerPixel), static_cast<int>(bitsPerSample), bigEndian);
            }
            for (unsigned int column = 0; column < runWidth; ++column) {
                const int x = across0 + static_cast<int>(column);
                if (x >= static_cast<int>(width)) {
                    break;
                }
                int red = 0;
                int green = 0;
                int blue = 0;
                int alpha = 255;
                const long long at = static_cast<long long>(column) * samplesPerPixel;
                if (paletted) {
                    const int index = ReadSample(samples.data(), at, static_cast<int>(bitsPerSample), bigEndian);
                    if (index < 0 || static_cast<std::size_t>(index) >= palette.red.size()) {
                        return false;
                    }
                    red = palette.red[static_cast<std::size_t>(index)];
                    green = palette.green[static_cast<std::size_t>(index)];
                    blue = palette.blue[static_cast<std::size_t>(index)];
                } else if (colourBySample) {
                    red = ScaleSample(ReadSample(samples.data(), at, static_cast<int>(bitsPerSample), bigEndian), fullScale);
                    green = ScaleSample(ReadSample(samples.data(), at + 1, static_cast<int>(bitsPerSample), bigEndian), fullScale);
                    blue = ScaleSample(ReadSample(samples.data(), at + 2, static_cast<int>(bitsPerSample), bigEndian), fullScale);
                } else {
                    const int level = ScaleSample(ReadSample(samples.data(), at, static_cast<int>(bitsPerSample), bigEndian), fullScale);
                    red = green = blue = inverted ? 255 - level : level;
                }
                if (hasAlpha) {
                    const long long alphaIndex = colourBySample ? at + 3 : at + colourSamples;
                    alpha = ScaleSample(ReadSample(samples.data(), alphaIndex, static_cast<int>(bitsPerSample), bigEndian), fullScale);
                    if (alpha != 0) {
                        sawAlpha = true;
                    }
                }
                const std::size_t pixel = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
                frame.bgra[pixel] = static_cast<unsigned char>(blue);
                frame.bgra[pixel + 1] = static_cast<unsigned char>(green);
                frame.bgra[pixel + 2] = static_cast<unsigned char>(red);
                frame.bgra[pixel + 3] = static_cast<unsigned char>(alpha);
            }
        }
    }
    if (hasAlpha && !sawAlpha) {
        // The last sample of a pixel is only a channel when something was written in it; a picture
        // where every one of them is nought never had transparency to keep.
        for (std::size_t at = 3; at < frame.bgra.size(); at += 4) {
            frame.bgra[at] = 255;
        }
    }
    image.format = "tiff";
    image.frames.push_back(std::move(frame));
    return true;
}

}  // namespace Runtime
