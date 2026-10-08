#pragma once

#include "rendering/image/ImageDecoder.h"

namespace Runtime {

// The picture the platform's own reader can turn into pixels, offered to the runtime as the decoder
// a host registers rather than as a reader the core carries. The container of an AVIF is read by the
// runtime itself, so the file is known to be one and known to be as big as it says, but the AV1
// codec that fills the picture in ships beside Windows rather than inside it: where that codec is
// installed this produces the pixels, and where it is not this refuses the file and the runtime
// draws the box the file says the picture would have covered.
//
// It reads AVIF only. Every other format the runtime knows is read by a reader of its own, and a
// format no reader here knows is refused rather than handed to the platform to be guessed at.
bool DecodeImageWithWic(const unsigned char* data, int size, std::string_view format, DecodedImage& image);

// Registers the reader above as the decoder a host supplies. A program that never calls this carries
// no knowledge of the machine it runs on and refuses every file its own readers cannot read, which
// is what makes the runtime's behaviour a property of the program rather than of the machine.
void RegisterWicImageProvider();

}  // namespace Runtime
