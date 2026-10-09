#include "rendering/image/VideoStream.h"

#include "rendering/image/ImageCodec.h"
#include "rendering/image/Vp8Image.h"

#include <algorithm>
#include <cstring>

namespace Runtime {
namespace {

// A frame with no length written for it is held for a tenth of a second, which is the timing a
// container that states no rate still has to draw something for.
constexpr int DEFAULT_FRAME_MILLISECONDS = 100;
// The most frames one file may hold. A container is walked to find where its frames are, and a file
// with more frames in it than this is not a picture a document holds but a recording: the walk is
// stopped so that opening a file cannot run away with the time of the frame being drawn.
constexpr int MOST_FRAMES = 200000;

bool IsFour(const unsigned char* data, const char* name) {
    return std::memcmp(data, name, 4) == 0;
}

}  // namespace

bool VideoStream::ReadAt(long long offset, void* out, std::size_t count) {
    if (offset < 0 || count > static_cast<std::size_t>(fileSize) || offset + static_cast<long long>(count) > fileSize) {
        return false;
    }
    file.clear();
    file.seekg(offset, std::ios::beg);
    file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(count));
    return file.good() || file.gcount() == static_cast<std::streamsize>(count);
}

bool VideoStream::ReadByte(long long offset, unsigned& value) {
    unsigned char byte = 0;
    if (!ReadAt(offset, &byte, 1)) {
        return false;
    }
    value = byte;
    return true;
}

bool VideoStream::ReadFour(long long offset, unsigned& value) {
    // Every figure a container writes is little-endian, whichever machine wrote it.
    unsigned char bytes[4] = {0, 0, 0, 0};
    if (!ReadAt(offset, bytes, 4)) {
        return false;
    }
    value = static_cast<unsigned>(bytes[0]) | (static_cast<unsigned>(bytes[1]) << 8) | (static_cast<unsigned>(bytes[2]) << 16) |
            (static_cast<unsigned>(bytes[3]) << 24);
    return true;
}

bool VideoStream::ReadEight(long long offset, unsigned long long& value) {
    unsigned low = 0;
    unsigned high = 0;
    if (!ReadFour(offset, low) || !ReadFour(offset + 4, high)) {
        return false;
    }
    value = static_cast<unsigned long long>(low) | (static_cast<unsigned long long>(high) << 32);
    return true;
}

bool VideoStream::ReadChunkHeader(long long offset, std::string& name, unsigned& size, long long& payload) {
    unsigned char head[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (!ReadAt(offset, head, 8)) {
        return false;
    }
    name.assign(reinterpret_cast<const char*>(head), 4);
    size = static_cast<unsigned>(head[4]) | (static_cast<unsigned>(head[5]) << 8) | (static_cast<unsigned>(head[6]) << 16) |
           (static_cast<unsigned>(head[7]) << 24);
    payload = offset + 8;
    return true;
}

void VideoStream::Close() {
    if (file.is_open()) {
        file.close();
    }
    path.clear();
    format.clear();
    codec.clear();
    offsets.clear();
    delays.clear();
    frameWidth = 0;
    frameHeight = 0;
    durationMs = 0;
    held = {};
    heldIndex = -1;
    fileSize = 0;
}

void VideoStream::SpreadDelays(const std::vector<long long>& stamps, long long unitNumerator, long long unitDenominator) {
    delays.assign(offsets.size(), DEFAULT_FRAME_MILLISECONDS);
    if (stamps.size() != offsets.size() || unitNumerator <= 0 || unitDenominator <= 0) {
        durationMs = static_cast<unsigned long long>(delays.size()) * DEFAULT_FRAME_MILLISECONDS;
        return;
    }
    durationMs = 0;
    for (std::size_t index = 0; index < stamps.size(); ++index) {
        // A frame is held until the moment the next one begins, and the last frame for as long as
        // the one before it was held, because a container says when its frames begin rather than how
        // long each of them lasts.
        const long long span = index + 1 < stamps.size() ? stamps[index + 1] - stamps[index] : stamps[index] - stamps[index - 1];
        const long long milliseconds = span * unitNumerator * 1000 / unitDenominator;
        delays[index] = static_cast<int>(std::clamp<long long>(milliseconds, 1, 60000));
        durationMs += static_cast<unsigned long long>(delays[index]);
    }
}

bool VideoStream::ReadIvf() {
    // A stream of frames: what the frames are, how big the picture is, the rate their own moments are
    // written at, and then a run of frames of the size and the moment each one carries.
    unsigned char head[32] = {};
    if (fileSize < 32 || !ReadAt(0, head, 32) || !IsFour(head, "DKIF")) {
        return false;
    }
    const unsigned headerLength = static_cast<unsigned>(head[6]) | (static_cast<unsigned>(head[7]) << 8);
    if (headerLength < 32 || static_cast<long long>(headerLength) > fileSize) {
        return false;
    }
    format = "ivf";
    codec.assign(reinterpret_cast<const char*>(head + 8), 4);
    frameWidth = static_cast<int>(static_cast<unsigned>(head[12]) | (static_cast<unsigned>(head[13]) << 8));
    frameHeight = static_cast<int>(static_cast<unsigned>(head[14]) | (static_cast<unsigned>(head[15]) << 8));
    const unsigned rateDenominator = static_cast<unsigned>(head[16]) | (static_cast<unsigned>(head[17]) << 8) |
                                     (static_cast<unsigned>(head[18]) << 16) | (static_cast<unsigned>(head[19]) << 24);
    const unsigned rateNumerator = static_cast<unsigned>(head[20]) | (static_cast<unsigned>(head[21]) << 8) |
                                   (static_cast<unsigned>(head[22]) << 16) | (static_cast<unsigned>(head[23]) << 24);
    // Only a stream of whole VP8 pictures is opened here: a picture that leans on an earlier one
    // cannot be read out of the middle of the file, and a run of frames with the ones between them
    // missing is not the picture the file holds.
    if (codec != "VP80" && codec != "VP8 ") {
        return false;
    }
    std::vector<long long> stamps;
    long long offset = static_cast<long long>(headerLength);
    while (offset + 12 <= fileSize && static_cast<int>(offsets.size()) < MOST_FRAMES) {
        unsigned size = 0;
        unsigned long long stamp = 0;
        if (!ReadFour(offset, size) || !ReadEight(offset + 4, stamp) || size == 0) {
            break;
        }
        const long long payload = offset + 12;
        if (payload + static_cast<long long>(size) > fileSize) {
            break;
        }
        // The first three bytes of a whole VP8 picture are the start code the format writes at the
        // front of one, and the lowest bit of the first byte says whether the picture stands alone.
        unsigned char mark[4] = {};
        if (!ReadAt(payload, mark, 4) || mark[3] != 0x9d || (mark[0] & 1u) != 0) {
            return false;
        }
        offsets.push_back(FrameRange{payload, static_cast<long long>(size)});
        stamps.push_back(static_cast<long long>(stamp));
        // The frames of an IVF stand one after another with nothing between them, unlike the chunks
        // of a RIFF file, which are each padded to an even length.
        offset = payload + static_cast<long long>(size);
    }
    if (offsets.empty()) {
        return false;
    }
    SpreadDelays(stamps, static_cast<long long>(rateNumerator), static_cast<long long>(rateDenominator));
    return true;
}

bool VideoStream::ReadAvi() {
    // An AVI is a RIFF file whose header list says what the picture is and whose movi list holds the
    // frames themselves, one chunk to a frame.
    unsigned char head[12] = {};
    if (fileSize < 12 || !ReadAt(0, head, 12) || !IsFour(head, "RIFF") || !IsFour(head + 8, "AVI ")) {
        return false;
    }
    format = "avi";
    unsigned microsecondsPerFrame = 0;
    long long moviStart = 0;
    long long moviEnd = 0;
    long long walk = 12;
    while (walk + 8 <= fileSize) {
        std::string name;
        unsigned size = 0;
        long long payload = 0;
        if (!ReadChunkHeader(walk, name, size, payload)) {
            break;
        }
        const long long end = std::min(payload + static_cast<long long>(size), fileSize);
        if (name == "LIST" && payload + 4 <= fileSize) {
            unsigned char kind[4] = {};
            if (ReadAt(payload, kind, 4)) {
                if (IsFour(kind, "hdrl")) {
                    // The header list: the rate every frame is held for, and the name of the codec
                    // the frames are written in.
                    long long inner = payload + 4;
                    while (inner + 8 <= end) {
                        std::string childName;
                        unsigned childSize = 0;
                        long long childPayload = 0;
                        if (!ReadChunkHeader(inner, childName, childSize, childPayload)) {
                            break;
                        }
                        if (childName == "avih" && childSize >= 40) {
                            // The one figure wanted here is the rate a frame is held at; the size of
                            // the picture is read off the stream's own format header below, which
                            // states it in the pixels the frames are written in.
                            ReadFour(childPayload + 0, microsecondsPerFrame);
                        } else if (childName == "LIST" && childSize >= 4) {
                            long long stream = childPayload + 4;
                            while (stream + 8 <= childPayload + static_cast<long long>(childSize)) {
                                std::string streamName;
                                unsigned streamSize = 0;
                                long long streamPayload = 0;
                                if (!ReadChunkHeader(stream, streamName, streamSize, streamPayload)) {
                                    break;
                                }
                                if (streamName == "strh" && streamSize >= 8) {
                                    unsigned char names[8] = {};
                                    if (ReadAt(streamPayload, names, 8)) {
                                        // The handler names the codec of the frames, and the type
                                        // says they are pictures rather than sound.
                                        if (IsFour(names, "vids")) {
                                            codec.assign(reinterpret_cast<const char*>(names + 4), 4);
                                        }
                                    }
                                } else if (streamName == "strf" && streamSize >= 12) {
                                    unsigned width = 0;
                                    unsigned height = 0;
                                    if (ReadFour(streamPayload + 4, width) && ReadFour(streamPayload + 8, height)) {
                                        frameWidth = static_cast<int>(width);
                                        frameHeight = static_cast<int>(height);
                                    }
                                }
                                stream = streamPayload + static_cast<long long>(streamSize);
                                if (stream % 2 != 0) {
                                    ++stream;
                                }
                            }
                        }
                        inner = childPayload + static_cast<long long>(childSize);
                        if (inner % 2 != 0) {
                            ++inner;
                        }
                    }
                } else if (IsFour(kind, "movi")) {
                    moviStart = payload + 4;
                    moviEnd = end;
                }
            }
        }
        walk = payload + static_cast<long long>(size);
        if (walk % 2 != 0) {
            ++walk;
        }
    }
    // Only a stream of whole JPEG pictures is opened here, for the same reason a stream of VP8
    // pictures has to be a stream of whole ones: a frame read out of the middle of a file has to be
    // a frame by itself, and Motion JPEG is exactly the codec whose frames are.
    if (codec != "MJPG" && codec != "mjpg" && codec != "JPEG" && codec != "jpeg") {
        return false;
    }
    if (moviStart == 0 || moviStart >= moviEnd) {
        return false;
    }
    long long frame = moviStart;
    while (frame + 8 <= moviEnd && static_cast<int>(offsets.size()) < MOST_FRAMES) {
        std::string name;
        unsigned size = 0;
        long long payload = 0;
        if (!ReadChunkHeader(frame, name, size, payload)) {
            break;
        }
        const long long end = payload + static_cast<long long>(size);
        if (end > moviEnd) {
            break;
        }
        // A frame's chunk is named for the stream it belongs to and then for what it holds: a
        // compressed picture is a `dc` and an uncompressed one a `db`, and Motion JPEG is always the
        // former because a JPEG is already compressed.
        if (name.size() == 4 && name[2] == 'd' && (name[3] == 'c' || name[3] == 'b')) {
            unsigned char mark[3] = {};
            if (!ReadAt(payload, mark, 3) || mark[0] != 0xFF || mark[1] != 0xD8 || mark[2] != 0xFF) {
                return false;
            }
            offsets.push_back(FrameRange{payload, static_cast<long long>(size)});
        }
        frame = end;
        if (frame % 2 != 0) {
            ++frame;
        }
    }
    if (offsets.empty()) {
        return false;
    }
    const int delay = microsecondsPerFrame > 0 ? static_cast<int>(std::max<unsigned>(1u, microsecondsPerFrame / 1000u))
                                               : DEFAULT_FRAME_MILLISECONDS;
    delays.assign(offsets.size(), delay);
    durationMs = static_cast<unsigned long long>(delays.size()) * static_cast<unsigned long long>(delay);
    return true;
}

bool VideoStream::Open(const std::string& source) {
    Close();
    file.open(source, std::ios::binary);
    if (!file) {
        file.clear();
        return false;
    }
    file.seekg(0, std::ios::end);
    fileSize = static_cast<long long>(file.tellg());
    if (fileSize < 12) {
        Close();
        return false;
    }
    if (!ReadAvi() && !ReadIvf()) {
        Close();
        return false;
    }
    if (frameWidth <= 0 || frameHeight <= 0 || offsets.empty()) {
        Close();
        return false;
    }
    path = source;
    return true;
}

int VideoStream::DelayAt(int index) const {
    if (index < 0 || index >= static_cast<int>(delays.size())) {
        return DEFAULT_FRAME_MILLISECONDS;
    }
    return delays[static_cast<std::size_t>(index)];
}

const ImageFrame* VideoStream::FrameAt(int index) {
    if (index < 0 || index >= static_cast<int>(offsets.size())) {
        return nullptr;
    }
    if (index == heldIndex && !held.bgra.empty()) {
        // The frame already in hand is the frame that was asked for, which is the whole of what makes
        // a picture standing on one frame cost nothing to stand there.
        return &held;
    }
    const FrameRange& range = offsets[static_cast<std::size_t>(index)];
    std::vector<unsigned char> payload(static_cast<std::size_t>(range.size));
    if (!ReadAt(range.offset, payload.data(), payload.size())) {
        return nullptr;
    }
    bool read = false;
    if (codec == "VP80" || codec == "VP8 ") {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        read = DecodeVp8Frame(payload.data(), static_cast<int>(payload.size()), width, height, pixels);
        if (read) {
            held = ImageFrame();
            held.width = width;
            held.height = height;
            held.delayMs = delays[static_cast<std::size_t>(index)];
            held.bgra = std::move(pixels);
        }
    } else {
        DecodedImage decoded;
        read = DecodeJpegImage(payload.data(), static_cast<int>(payload.size()), decoded) && !decoded.frames.empty();
        if (read) {
            held = std::move(decoded.frames.front());
            if (held.delayMs <= 0) {
                held.delayMs = delays[static_cast<std::size_t>(index)];
            }
        }
    }
    if (!read) {
        held = ImageFrame();
        heldIndex = -1;
        return nullptr;
    }
    heldIndex = index;
    return &held;
}

}  // namespace Runtime
