#pragma once

#include "rendering/image/ImageDecoder.h"

#include <fstream>
#include <string>
#include <vector>

namespace Runtime {

// A picture whose frames are read from a file one at a time rather than all at once.
//
// A still picture and an animation are both held whole: a GIF is a handful of frames and a picture
// read from a stream of them is not. A minute of a moving picture is thousands of frames, and a
// frame of a large one is megabytes, so a run of frames held all at once is more than a document
// has any right to keep. What a container is asked for here is therefore where its frames are, not
// what is in them: the frames themselves are read when the frame is wanted, and only the frame the
// picture is standing on is held.
//
// A frame is read on its own only when the file's own frames stand on their own. Motion JPEG is
// that case exactly — every frame of it is a whole JPEG picture, so any frame can be read without
// the ones before it — and the VP8 frames of an IVF are read the same way, because the reader here
// takes whole pictures and refuses the ones that lean on an earlier frame. A stream whose frames
// lean on each other is not opened, so the element that asked is free to read it the way it reads
// any other file rather than showing a run of frames with gaps in it.
class VideoStream {
public:
    // Opens a file whose frames are a run of whole pictures. False means the file is not a container
    // of them, or is one whose frames are not whole, and nothing about it is kept.
    bool Open(const std::string& path);

    bool Ready() const { return !path.empty(); }

    // How many frames the file holds, how big the picture is, and how long one pass through it
    // lasts — all of them read from the container, which writes down what its frames are before it
    // writes them down.
    int FrameCount() const { return static_cast<int>(offsets.size()); }
    int Width() const { return frameWidth; }
    int Height() const { return frameHeight; }
    unsigned long long DurationMilliseconds() const { return durationMs; }
    // How long the frame with this index is held for, in milliseconds: the span the container wrote
    // between this frame's moment and the next one's.
    int DelayAt(int index) const;
    // What the container is, for the record rather than for the drawing.
    const std::string& Format() const { return format; }

    // The frame with this index, read from the file now if it is not the one already held. The frame
    // stands until another one is asked for; nothing is returned for an index the file has no frame
    // for, or for a frame whose pixels could not be read.
    const ImageFrame* FrameAt(int index);

    // Shuts the file and forgets everything that was read of it, so the container can be pointed at
    // another file or let go of by a node whose picture has changed.
    void Close();

private:
    // Where one frame's bytes lie in the file.
    struct FrameRange {
        long long offset = 0;
        long long size = 0;
    };

    // Reading a run of bytes at a place in the file, which is how the container is walked and how a
    // frame is read out of the middle of it.
    bool ReadAt(long long offset, void* out, std::size_t count);
    bool ReadByte(long long offset, unsigned& value);
    bool ReadFour(long long offset, unsigned& value);
    bool ReadEight(long long offset, unsigned long long& value);
    // Reading a chunk header: the name of the chunk, how long its payload is, and where the payload
    // begins. Every container here is a run of these, nested inside one another.
    bool ReadChunkHeader(long long offset, std::string& name, unsigned& size, long long& payload);

    bool ReadAvi();
    bool ReadIvf();
    // The delay each frame is held for, from the moments the container says its frames begin at.
    void SpreadDelays(const std::vector<long long>& stamps, long long unitNumerator, long long unitDenominator);

    std::string path;
    std::string format;
    std::string codec;
    // The file itself, held open: a container is an index of where its frames lie, and a frame is
    // read out of the middle of the file when it is wanted rather than kept beside the index.
    std::ifstream file;
    long long fileSize = 0;
    std::vector<FrameRange> offsets;
    std::vector<int> delays;
    int frameWidth = 0;
    int frameHeight = 0;
    unsigned long long durationMs = 0;
    // The frame that was read last, so a picture standing on one frame is not read over and over
    // while it stands there.
    ImageFrame held;
    int heldIndex = -1;
};

}  // namespace Runtime
