#pragma once

#include <vector>

namespace Runtime {

// Reads a WebP lossless (VP8L) bitstream: the bytes that begin at the 0x2f signature, with no RIFF
// container around them. The picture comes back as BGRA rows without padding, and its stated size
// comes back whether or not the pixels could be read. False means the bitstream is not one this
// reader understands.
bool DecodeVp8LStream(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra);

}  // namespace Runtime
