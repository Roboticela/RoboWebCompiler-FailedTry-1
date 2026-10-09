#pragma once

#include "rendering/image/ImageDecoder.h"

namespace Runtime {

bool DecodePngImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeJpegImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeGifImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeWebpImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeBmpImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeIcoImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeTiffImage(const unsigned char* data, int size, DecodedImage& image);
// Reads the AVIF container and says what it holds. The pixels are not produced here: an AVIF is an
// AV1 picture, and the codec that turns one into pixels is not part of the runtime, so the frames
// are left empty and the format and the stated size are filled in.
bool DecodeAvifImage(const unsigned char* data, int size, DecodedImage& image);

}  // namespace Runtime
