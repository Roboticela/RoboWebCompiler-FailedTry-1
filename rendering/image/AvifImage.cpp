#include "rendering/image/ImageCodec.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <utility>

namespace Runtime {
namespace {

// An AVIF is read by the platform's own reader rather than by a decoder written here. Windows knows
// the container, and the AV1 codec that turns the bytes of a picture into pixels is a part of
// Windows that ships separately, as the "AV1 Video Extension": that is the only AV1 decoder the
// runtime can reach without a library of its own, so an AVIF is drawn where it is installed and
// refused where it is not, even when the file itself is sound.
class PlatformCom {
public:
    PlatformCom() : owned(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
    ~PlatformCom() {
        if (owned) {
            CoUninitialize();
        }
    }

    PlatformCom(const PlatformCom&) = delete;
    PlatformCom& operator=(const PlatformCom&) = delete;

private:
    // True when this call is the one that initialized the apartment, which is the case that owes a
    // matching uninitialize. A thread that was already initialized in another mode is left alone.
    bool owned = false;
};

// What the file says it is. The brand of a file whose header is `ftyp` names the picture it holds:
// `avif` for one still picture and `avis` for a sequence, and either one is an AVIF.
bool LooksLikeAvif(const unsigned char* data, int size) {
    if (size < 12 || std::memcmp(data + 4, "ftyp", 4) != 0) {
        return false;
    }
    return std::memcmp(data + 8, "avif", 4) == 0 || std::memcmp(data + 8, "avis", 4) == 0;
}

}  // namespace

bool DecodeAvifImage(const unsigned char* data, int size, DecodedImage& image) {
    if (data == nullptr || !LooksLikeAvif(data, size)) {
        return false;
    }
    const PlatformCom com;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()))) {
        return false;
    }
    ULONG written = 0;
    if (FAILED(stream->Write(data, static_cast<ULONG>(size), &written)) || written != static_cast<ULONG>(size)) {
        return false;
    }
    LARGE_INTEGER start{};
    if (FAILED(stream->Seek(start, STREAM_SEEK_SET, nullptr))) {
        return false;
    }
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()))) {
        return false;
    }
    // The first frame is the picture. A file that holds a run of them holds its first one here,
    // which is the frame a picture with no clock of its own shows.
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
        return false;
    }
    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width < 1 || height < 1) {
        return false;
    }
    // The pixels are asked for in the form the runtime draws in, with the alpha already folded into
    // every channel, so the work of making them that form is done by the decoder that wrote them.
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return false;
    }
    ImageFrame pixels;
    pixels.width = static_cast<int>(width);
    pixels.height = static_cast<int>(height);
    pixels.premultiplied = true;
    pixels.bgra.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0);
    const UINT stride = width * 4u;
    if (FAILED(converter->CopyPixels(nullptr, stride, stride * height, pixels.bgra.data()))) {
        return false;
    }
    image.format = "avif";
    image.frames.clear();
    image.frames.push_back(std::move(pixels));
    return true;
}

}  // namespace Runtime
