#pragma once

#include <vector>

namespace Runtime {

// Reads a WebP lossy frame: the bytes that begin at the ten-byte uncompressed frame tag, with no
// RIFF container around them. The picture comes back as BGRA rows without padding and fully opaque,
// because any transparency lives in the separate alpha plane of the container. The stated size comes
// back whether or not the pixels could be read. False means the frame is not one this reader
// understands.
bool DecodeVp8Frame(const unsigned char* data, int size, int& width, int& height, std::vector<unsigned char>& bgra);

}  // namespace Runtime
