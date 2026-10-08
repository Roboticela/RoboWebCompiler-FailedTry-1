#include "rendering/image/ImageCodec.h"

#include "rendering/image/Vp8Image.h"
#include "rendering/image/Vp8LImage.h"

#include <cstring>
#include <string>
#include <vector>

namespace Runtime {
namespace {

const int kMaxCanvasEdge = 16384;
const int kMaxFrames = 4096;

struct WebpChunk {
    char type[5] = {};
    const unsigned char* body = nullptr;
    int size = 0;
};

unsigned int ReadLe24(const unsigned char* data) {
    return static_cast<unsigned int>(data[0]) | (static_cast<unsigned int>(data[1]) << 8) | (static_cast<unsigned int>(data[2]) << 16);
}

unsigned int ReadLe32(const unsigned char* data) {
    return static_cast<unsigned int>(data[0]) | (static_cast<unsigned int>(data[1]) << 8) | (static_cast<unsigned int>(data[2]) << 16) |
           (static_cast<unsigned int>(data[3]) << 24);
}

bool IsChunkType(const WebpChunk& chunk, const char* type) {
    return std::memcmp(chunk.type, type, 4) == 0;
}

// Walks one run of subchunks: either the whole file from the twelve bytes of the RIFF header, or the
// body of an animation frame, which holds the same kinds of chunk as the file does. Chunk bodies sit
// on even boundaries, so a chunk of odd length is followed by a padding byte that is not part of it.
bool CollectChunks(const unsigned char* data, int size, int start, int end, std::vector<WebpChunk>& chunks) {
    for (int offset = start; offset + 8 <= end;) {
        WebpChunk chunk;
        std::memcpy(chunk.type, data + offset, 4);
        chunk.type[4] = '\0';
        const unsigned int chunkSize = ReadLe32(data + offset + 4);
        if (chunkSize > static_cast<unsigned int>(end - offset - 8)) {
            return false;
        }
        chunk.body = data + offset + 8;
        chunk.size = static_cast<int>(chunkSize);
        chunks.push_back(chunk);
        offset += 8 + static_cast<int>(chunkSize) + (static_cast<int>(chunkSize) & 1);
    }
    return true;
}

const WebpChunk* FindChunk(const std::vector<WebpChunk>& chunks, const char* type) {
    for (const WebpChunk& chunk : chunks) {
        if (IsChunkType(chunk, type)) {
            return &chunk;
        }
    }
    return nullptr;
}

// The alpha plane a lossy picture may carry beside it. It is either written out plainly, filtered the
// way a row of a picture is filtered, or compressed as a lossless picture whose green channel is the
// alpha the encoder wanted and whose color channels are there only to make that compress well.
bool DecodeAlphaPlane(const WebpChunk& chunk, int width, int height, std::vector<unsigned char>& alpha) {
    if (chunk.size < 1 || width < 1 || height < 1) {
        return false;
    }
    const int flags = chunk.body[0];
    const bool lossless = (flags & 0x01) != 0;
    const int method = (flags >> 2) & 0x03;
    const int preprocessing = (flags >> 4) & 0x03;
    const int pixels = width * height;
    alpha.assign(static_cast<std::size_t>(pixels), 255);
    if (lossless) {
        int readWidth = 0;
        int readHeight = 0;
        std::vector<unsigned char> bgra;
        if (!DecodeVp8LStream(chunk.body + 1, chunk.size - 1, readWidth, readHeight, bgra)) {
            return false;
        }
        if (readWidth != width || readHeight != height) {
            return false;
        }
        for (int index = 0; index < pixels; ++index) {
            alpha[static_cast<std::size_t>(index)] = bgra[static_cast<std::size_t>(index) * 4 + 1];
        }
        return true;
    }
    if (chunk.size - 1 < pixels) {
        return false;
    }
    const unsigned char* source = chunk.body + 1;
    for (int y = 0; y < height; ++y) {
        int predictor = 0;
        for (int x = 0; x < width; ++x) {
            const int filtered = source[y * width + x];
            int value = 0;
            switch (method) {
                case 0:
                    value = filtered;
                    break;
                case 1:
                    // The difference from the pixel to the left.
                    value = filtered + predictor;
                    predictor = value;
                    break;
                case 2:
                    // The difference from the pixel above.
                    value = filtered + (y > 0 ? alpha[static_cast<std::size_t>((y - 1) * width + x)] : 0);
                    predictor = value;
                    break;
                default: {
                    // The difference from the pixel above plus the one to the left minus the one
                    // above and to the left, which is a guess that follows a gradient.
                    int guess = 0;
                    if (y > 0) {
                        guess += alpha[static_cast<std::size_t>((y - 1) * width + x)];
                        if (x > 0) {
                            guess -= alpha[static_cast<std::size_t>((y - 1) * width + x - 1)];
                        }
                    }
                    if (x > 0) {
                        guess += alpha[static_cast<std::size_t>(y * width + x - 1)];
                    }
                    value = (filtered + guess) & 0xff;
                    break;
                }
            }
            alpha[static_cast<std::size_t>(y * width + x)] = static_cast<unsigned char>(value & 0xff);
            predictor = value;
        }
    }
    // A picture whose alpha was stored at seven bits comes back with a pattern of light and dark
    // added to it, so the step between two levels is not a hard edge. The pattern is the position of
    // the pixel itself, which does not have to be kept anywhere.
    if (preprocessing == 1) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int dither = ((x ^ y) & 1) ? 1 : 0;
                const int index = y * width + x;
                alpha[static_cast<std::size_t>(index)] = static_cast<unsigned char>((alpha[static_cast<std::size_t>(index)] & ~1) | dither);
            }
        }
    }
    return true;
}

void CompositeFrame(std::vector<unsigned char>& canvas, int canvasWidth, int canvasHeight, const std::vector<unsigned char>& pixels, int offsetX,
                    int offsetY, int width, int height, bool blend) {
    for (int y = 0; y < height; ++y) {
        const int destY = offsetY + y;
        if (destY < 0 || destY >= canvasHeight) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int destX = offsetX + x;
            if (destX < 0 || destX >= canvasWidth) {
                continue;
            }
            const std::size_t source = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::size_t dest = (static_cast<std::size_t>(destY) * canvasWidth + destX) * 4;
            const unsigned int alpha = pixels[source + 3];
            if (alpha == 0 && blend) {
                continue;
            }
            if (!blend || alpha == 255) {
                canvas[dest] = pixels[source];
                canvas[dest + 1] = pixels[source + 1];
                canvas[dest + 2] = pixels[source + 2];
                canvas[dest + 3] = pixels[source + 3];
                continue;
            }
            // Source over destination, with the destination's own alpha weighed in as well so a
            // partly transparent frame over a partly transparent canvas keeps its proportions.
            const int destAlpha = canvas[dest + 3];
            const int outAlpha = alpha + destAlpha * (255 - alpha) / 255;
            if (outAlpha == 0) {
                canvas[dest] = 0;
                canvas[dest + 1] = 0;
                canvas[dest + 2] = 0;
                canvas[dest + 3] = 0;
                continue;
            }
            for (int channel = 0; channel < 3; ++channel) {
                const int over = pixels[source + channel];
                const int under = canvas[dest + channel];
                canvas[dest + channel] = static_cast<unsigned char>((over * alpha + under * destAlpha * (255 - alpha) / 255) / outAlpha);
            }
            canvas[dest + 3] = static_cast<unsigned char>(outAlpha);
        }
    }
}

// The pixels of one picture, lossless or lossy, with any alpha plane it carries laid over the result.
bool DecodePicture(const std::vector<WebpChunk>& chunks, int& width, int& height, std::vector<unsigned char>& pixels) {
    const WebpChunk* lossless = FindChunk(chunks, "VP8L");
    const WebpChunk* lossy = FindChunk(chunks, "VP8 ");
    if (lossless != nullptr) {
        return DecodeVp8LStream(lossless->body, lossless->size, width, height, pixels);
    }
    if (lossy == nullptr) {
        return false;
    }
    int frameWidth = 0;
    int frameHeight = 0;
    std::vector<unsigned char> bgra;
    if (!DecodeVp8Frame(lossy->body, lossy->size, frameWidth, frameHeight, bgra)) {
        width = frameWidth;
        height = frameHeight;
        return false;
    }
    width = frameWidth;
    height = frameHeight;
    const WebpChunk* alpha = FindChunk(chunks, "ALPH");
    if (alpha != nullptr) {
        std::vector<unsigned char> plane;
        if (DecodeAlphaPlane(*alpha, width, height, plane)) {
            for (std::size_t index = 0; index + 3 < bgra.size(); index += 4) {
                bgra[index + 3] = plane[index / 4];
            }
        }
    }
    pixels.swap(bgra);
    return true;
}

}  // namespace

bool DecodeWebpImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || size < 20 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WEBP", 4) != 0) {
        return false;
    }
    const unsigned int declared = ReadLe32(data + 4);
    const int end = (declared + 8 <= static_cast<unsigned int>(size)) ? static_cast<int>(declared + 8) : size;
    std::vector<WebpChunk> chunks;
    if (!CollectChunks(data, size, 12, end, chunks)) {
        return false;
    }
    const WebpChunk* extended = FindChunk(chunks, "VP8X");
    if (extended == nullptr) {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        if (!DecodePicture(chunks, width, height, pixels)) {
            return false;
        }
        ImageFrame frame;
        frame.width = width;
        frame.height = height;
        frame.bgra = std::move(pixels);
        image.format = "webp";
        image.declaredWidth = width;
        image.declaredHeight = height;
        image.frames.push_back(std::move(frame));
        return true;
    }
    if (extended->size < 10) {
        return false;
    }
    // Every size in this header is one less than the size it states, because a picture of no width
    // or height could not be written down otherwise.
    const int canvasWidth = static_cast<int>(ReadLe24(extended->body + 4)) + 1;
    const int canvasHeight = static_cast<int>(ReadLe24(extended->body + 7)) + 1;
    if (canvasWidth < 1 || canvasHeight < 1 || canvasWidth > kMaxCanvasEdge || canvasHeight > kMaxCanvasEdge) {
        return false;
    }
    const WebpChunk* animation = FindChunk(chunks, "ANIM");
    if (animation == nullptr) {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
        if (!DecodePicture(chunks, width, height, pixels)) {
            return false;
        }
        ImageFrame frame;
        frame.width = width;
        frame.height = height;
        frame.bgra = std::move(pixels);
        image.format = "webp";
        image.declaredWidth = width;
        image.declaredHeight = height;
        image.frames.push_back(std::move(frame));
        return true;
    }
    // An animation is a canvas and a run of frames laid onto it, each frame covering a rectangle of
    // it rather than the whole of it. What a frame leaves alone is what the frame before it left
    // there, so the canvas has to be carried from one frame to the next.
    unsigned int background = 0;
    if (animation->size >= 4) {
        background = ReadLe32(animation->body);
    }
    std::vector<unsigned char> canvas(static_cast<std::size_t>(canvasWidth) * canvasHeight * 4, 0);
    for (int index = 0; index < canvasWidth * canvasHeight; ++index) {
        canvas[static_cast<std::size_t>(index) * 4] = static_cast<unsigned char>(background & 0xff);
        canvas[static_cast<std::size_t>(index) * 4 + 1] = static_cast<unsigned char>((background >> 8) & 0xff);
        canvas[static_cast<std::size_t>(index) * 4 + 2] = static_cast<unsigned char>((background >> 16) & 0xff);
        canvas[static_cast<std::size_t>(index) * 4 + 3] = static_cast<unsigned char>((background >> 24) & 0xff);
    }
    int frameCount = 0;
    for (const WebpChunk& chunk : chunks) {
        if (!IsChunkType(chunk, "ANMF")) {
            continue;
        }
        if (++frameCount > kMaxFrames || chunk.size < 16) {
            return false;
        }
        const int offsetX = static_cast<int>(ReadLe24(chunk.body)) * 2;
        const int offsetY = static_cast<int>(ReadLe24(chunk.body + 3)) * 2;
        const int frameWidth = static_cast<int>(ReadLe24(chunk.body + 6)) + 1;
        const int frameHeight = static_cast<int>(ReadLe24(chunk.body + 9)) + 1;
        const int duration = static_cast<int>(ReadLe24(chunk.body + 12));
        // The first bit says whether the canvas keeps what the frame drew over, and the second says
        // whether the frame is laid down with its own transparency or simply replaces what it
        // covers. Zero on the second bit is the blend, which reads backwards from the obvious.
        const int flags = chunk.body[15];
        const bool dispose = (flags & 0x01) != 0;
        const bool blend = (flags & 0x02) == 0;
        std::vector<WebpChunk> picture;
        if (!CollectChunks(data, size, static_cast<int>(chunk.body + 16 - data), static_cast<int>(chunk.body + chunk.size - data), picture)) {
            return false;
        }
        std::vector<unsigned char> pixels;
        int readWidth = 0;
        int readHeight = 0;
        if (!DecodePicture(picture, readWidth, readHeight, pixels)) {
            return false;
        }
        if (readWidth != frameWidth || readHeight != frameHeight) {
            return false;
        }
        if (dispose) {
            for (int y = 0; y < frameHeight; ++y) {
                const int destY = offsetY + y;
                if (destY < 0 || destY >= canvasHeight) {
                    continue;
                }
                for (int x = 0; x < frameWidth; ++x) {
                    const int destX = offsetX + x;
                    if (destX < 0 || destX >= canvasWidth) {
                        continue;
                    }
                    const std::size_t dest = (static_cast<std::size_t>(destY) * canvasWidth + destX) * 4;
                    canvas[dest] = 0;
                    canvas[dest + 1] = 0;
                    canvas[dest + 2] = 0;
                    canvas[dest + 3] = 0;
                }
            }
        }
        CompositeFrame(canvas, canvasWidth, canvasHeight, pixels, offsetX, offsetY, frameWidth, frameHeight, blend);
        ImageFrame frame;
        frame.width = canvasWidth;
        frame.height = canvasHeight;
        frame.delayMs = duration > 0 ? duration : 100;
        frame.bgra = canvas;
        image.frames.push_back(std::move(frame));
    }
    if (image.frames.empty()) {
        return false;
    }
    image.format = "webp";
    image.declaredWidth = canvasWidth;
    image.declaredHeight = canvasHeight;
    return true;
}

}  // namespace Runtime
