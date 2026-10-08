#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Runtime {

struct ImageFrame {
    int width = 0;
    int height = 0;
    int delayMs = 0;
    // Whether the channels of `bgra` already have their alpha folded into them. Both forms are read
    // by the runtime — a decoder that lays a picture down by compositing one shape over another
    // accumulates premultiplied pixels, while one that writes the channels a file held writes them
    // straight — so the frame says which it is, and the buffer a picture is finally drawn from is
    // made the form the platform composites either way.
    bool premultiplied = false;
    std::vector<unsigned char> bgra;
};

struct DecodedImage {
    std::string format;
    // What the file itself says about the picture, read from its header rather than from its pixels.
    // A file whose pixels the runtime cannot produce still states how big the picture in it is, and
    // a node with no size of its own is as big as that statement: the picture lays out as the picture
    // it holds and the box it could not fill stands where the picture would have. Zero means the file
    // did not say, which is the case for every format whose size is only known once it is read.
    int declaredWidth = 0;
    int declaredHeight = 0;
    std::vector<ImageFrame> frames;
};

// A decoder for a picture the core cannot turn into pixels by itself. An AVIF is the case this
// exists for: the container is read here, while the AV1 codec that fills the pixels in it is not
// something the runtime carries, so who reads it is a question about the machine rather than about
// the file. A host that can read one registers a decoder, and a host that cannot leaves it unset,
// which makes the answer a property of the program rather than of the operating system underneath
// it. The decoder is offered the whole file, and the format the core read from it, after every
// decoder written here has passed on it; it answers true when it filled in the frames.
typedef bool (*ImageFormatProvider)(const unsigned char* data, int size, std::string_view format, DecodedImage& image);

// The one decoder a host may register, or nothing when none is registered. It is a single slot
// because it stands for the machine's ability to read a picture, and a machine has one answer.
void SetImageFormatProvider(ImageFormatProvider provider);
ImageFormatProvider GetImageFormatProvider();

struct SvgDecodeOptions {
    int maxEdge = 0;
    unsigned char currentRed = 0;
    unsigned char currentGreen = 0;
    unsigned char currentBlue = 0;
};

bool DecodeImageBytes(const unsigned char* data, int size, DecodedImage& image);
bool DecodeImageFile(const std::string& path, DecodedImage& image);
bool DecodeSvgBytes(const unsigned char* data, int size, DecodedImage& image, const SvgDecodeOptions& options = {});
std::string VectorXmlFromSvg(std::string_view svg, std::string_view name, unsigned char red, unsigned char green, unsigned char blue);

}  // namespace Runtime
