#pragma once

#include "rendering/image/ImageDecoder.h"

namespace Runtime {

bool DecodePngImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeJpegImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeGifImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeWebpImage(const unsigned char* data, int size, DecodedImage& image);
bool DecodeAvifImage(const unsigned char* data, int size, DecodedImage& image);

}  // namespace Runtime
