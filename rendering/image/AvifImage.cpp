#include "rendering/image/ImageCodec.h"

#include <cstring>
#include <string>

namespace Runtime {
namespace {

// An AVIF is an AV1 picture carried in an ISOBMFF box, which is the same kind of box a film is
// carried in: a sequence of boxes, each one a length, a name, and a body, and each body made of
// boxes of its own. The runtime reads that container here, in the same portable code every other
// reader is written in, so a file is known to be an AVIF, known to be as big as it says, and known
// to hold a picture or a run of them wherever it is opened.
//
// What is not read here is the AV1 codec that fills the picture in. That is a decoder of its own,
// and until one is written the pixels arrive from a decoder the host registered (see
// ImageFormatProvider), which on Windows is the platform reader. Reading the container here means
// the runtime's knowledge of the file is its own rather than the machine's: a file whose pixels no
// decoder can produce still lays out as the picture it holds, because the box it stands in is the
// box the file states rather than the box every unreadable file is drawn as.

unsigned int ReadBe32(const unsigned char* data) {
    return (static_cast<unsigned int>(data[0]) << 24) | (static_cast<unsigned int>(data[1]) << 16) |
           (static_cast<unsigned int>(data[2]) << 8) | static_cast<unsigned int>(data[3]);
}

unsigned long long ReadBe64(const unsigned char* data) {
    unsigned long long value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8) | data[index];
    }
    return value;
}

// The number of bytes a FullBox spends on its version and flags before its body begins.
constexpr int kFullBoxHeader = 4;
constexpr int kBoxHeader = 8;
constexpr int kLargeBoxHeader = 16;
// The largest edge a picture read here may declare, which is the same ceiling every other reading
// of a file in the runtime holds itself to.
constexpr int kMaxEdge = 8192;

// One box: what it is, where its body is, how long that body is, and where the box after it begins.
// `next` is kept as an absolute offset so a walk can step from one box to the next without knowing
// how the one it just read was written.
struct Box {
    std::string type;
    int bodyOffset = 0;
    int size = 0;
    int next = 0;
};

// Reads the box beginning at `offset`. A length of one means the real length is written in eight
// bytes after the name, and a length of nothing means the box runs to the end of its parent, which
// is how a file being written as it is read lets its last box grow.
bool ReadBox(const unsigned char* data, int size, int offset, Box& box) {
    if (offset < 0 || offset + kBoxHeader > size) {
        return false;
    }
    unsigned long long length = ReadBe32(data + offset);
    int header = kBoxHeader;
    if (length == 1) {
        if (offset + kLargeBoxHeader > size) {
            return false;
        }
        length = ReadBe64(data + offset + 8);
        header = kLargeBoxHeader;
    } else if (length == 0) {
        length = static_cast<unsigned long long>(size - offset);
    }
    if (length < static_cast<unsigned long long>(header) || length > static_cast<unsigned long long>(size - offset)) {
        return false;
    }
    box.type.assign(reinterpret_cast<const char*>(data + offset + 4), 4);
    box.bodyOffset = offset + header;
    box.size = static_cast<int>(length) - header;
    box.next = offset + static_cast<int>(length);
    return true;
}

// The first box of this name among the boxes written between two offsets, or nothing when there is
// no such box. A box that runs past the end of the space being searched ends the search, because
// what follows it cannot be a box where it was claimed to be.
bool FindBox(const unsigned char* data, int size, int begin, int end, const char* type, Box& found) {
    int at = begin;
    while (at + kBoxHeader <= end) {
        Box box;
        if (!ReadBox(data, size, at, box) || box.next <= at || box.next > end) {
            return false;
        }
        if (box.type == type) {
            found = box;
            return true;
        }
        at = box.next;
    }
    return false;
}

// What the file says it is: `avif` for one picture, `avis` for a run of them, and nothing for a box
// of a kind the runtime does not read. A file names every brand it satisfies, the one it was written
// as first and the ones it is also readable as after it, so any of them is an answer. Which picture
// a file holds is the one thing about it that is known before a byte of its codec is read.
enum class AvifBrand {
    NONE,
    STILL,
    ANIMATED,
};

AvifBrand ReadBrand(const unsigned char* data, const Box& ftyp) {
    bool still = false;
    bool animated = false;
    // The brand the file was written as is the first word of the body; the words after the major
    // brand and its version are the brands it is also compatible with.
    for (int at = 0; at + 4 <= ftyp.size; at += 4) {
        const char* word = reinterpret_cast<const char*>(data + ftyp.bodyOffset + at);
        if (std::memcmp(word, "avif", 4) == 0) {
            still = true;
        }
        if (std::memcmp(word, "avis", 4) == 0) {
            animated = true;
        }
    }
    if (animated) {
        return AvifBrand::ANIMATED;
    }
    if (still) {
        return AvifBrand::STILL;
    }
    return AvifBrand::NONE;
}

// The size a picture states in one `ispe` box: a version and flags, then the width and the height,
// each written as four bytes.
bool ReadIspe(const unsigned char* data, const Box& ispe, int& width, int& height) {
    if (ispe.size < kFullBoxHeader + 8) {
        return false;
    }
    width = static_cast<int>(ReadBe32(data + ispe.bodyOffset + kFullBoxHeader));
    height = static_cast<int>(ReadBe32(data + ispe.bodyOffset + kFullBoxHeader + 4));
    return width > 0 && height > 0 && width <= kMaxEdge && height <= kMaxEdge;
}

// The size of the picture a still file holds, taken from the properties the file attaches to it. A
// picture built out of tiles states a size for the whole of it and a size for every tile in it, and
// the whole is the largest, so the largest statement is the picture rather than a part of it.
bool ReadStillSize(const unsigned char* data, int size, const Box& meta, int& width, int& height) {
    Box iprp;
    const int metaBegin = meta.bodyOffset + kFullBoxHeader;
    const int metaEnd = meta.bodyOffset + meta.size;
    if (metaBegin > metaEnd || !FindBox(data, size, metaBegin, metaEnd, "iprp", iprp)) {
        return false;
    }
    Box ipco;
    if (!FindBox(data, size, iprp.bodyOffset, iprp.bodyOffset + iprp.size, "ipco", ipco)) {
        return false;
    }
    int bestWidth = 0;
    int bestHeight = 0;
    int at = ipco.bodyOffset;
    const int end = ipco.bodyOffset + ipco.size;
    while (at + kBoxHeader <= end) {
        Box property;
        if (!ReadBox(data, size, at, property) || property.next <= at || property.next > end) {
            break;
        }
        if (property.type == "ispe") {
            int statedWidth = 0;
            int statedHeight = 0;
            if (ReadIspe(data, property, statedWidth, statedHeight) &&
                static_cast<long long>(statedWidth) * statedHeight > static_cast<long long>(bestWidth) * bestHeight) {
                bestWidth = statedWidth;
                bestHeight = statedHeight;
            }
        }
        at = property.next;
    }
    width = bestWidth;
    height = bestHeight;
    return bestWidth > 0 && bestHeight > 0;
}

// The size a run of pictures holds, taken from the track the pictures are laid out on. A run is a
// film rather than a set of items, so its size is stated by the track header instead of by an item
// property. Past the version and flags come several times and numbers, a matrix, and then the width
// and the height, each written as sixteen digits of which the top half are whole pixels. A version
// one header spends eight bytes on each of the times where a version zero header spends four.
bool ReadTrackSize(const unsigned char* data, int size, int begin, int end, int& width, int& height) {
    Box moov;
    if (!FindBox(data, size, begin, end, "moov", moov)) {
        return false;
    }
    Box trak;
    if (!FindBox(data, size, moov.bodyOffset, moov.bodyOffset + moov.size, "trak", trak)) {
        return false;
    }
    Box tkhd;
    if (!FindBox(data, size, trak.bodyOffset, trak.bodyOffset + trak.size, "tkhd", tkhd)) {
        return false;
    }
    const int version = tkhd.size >= 1 ? data[tkhd.bodyOffset] : 0;
    const int sizeOffset = version == 1 ? kFullBoxHeader + 88 : kFullBoxHeader + 76;
    if (tkhd.size < sizeOffset + 8) {
        return false;
    }
    // The digits after the point are the fraction of a pixel, rounded to the pixel nearest rather
    // than dropped, so a picture that is not a whole number of pixels wide is drawn at the size it
    // was meant to be.
    const unsigned int statedWidth = ReadBe32(data + tkhd.bodyOffset + sizeOffset);
    const unsigned int statedHeight = ReadBe32(data + tkhd.bodyOffset + sizeOffset + 4);
    width = static_cast<int>((statedWidth + 0x8000u) >> 16);
    height = static_cast<int>((statedHeight + 0x8000u) >> 16);
    return width > 0 && height > 0 && width <= kMaxEdge && height <= kMaxEdge;
}

}  // namespace

bool DecodeAvifImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < kBoxHeader) {
        return false;
    }
    Box ftyp;
    if (!FindBox(data, size, 0, size, "ftyp", ftyp)) {
        return false;
    }
    const AvifBrand brand = ReadBrand(data, ftyp);
    if (brand == AvifBrand::NONE) {
        return false;
    }
    // The file is an AVIF whether or not its pixels can be read, so the name it is known by and the
    // size it states are kept either way. What is left behind is the frames, which a decoder that
    // knows AV1 fills in and no decoder written here does.
    image.format = brand == AvifBrand::ANIMATED ? "avis" : "avif";
    Box meta;
    if (FindBox(data, size, 0, size, "meta", meta)) {
        ReadStillSize(data, size, meta, image.declaredWidth, image.declaredHeight);
    }
    if (image.declaredWidth < 1 || image.declaredHeight < 1) {
        ReadTrackSize(data, size, 0, size, image.declaredWidth, image.declaredHeight);
    }
    return false;
}

}  // namespace Runtime
