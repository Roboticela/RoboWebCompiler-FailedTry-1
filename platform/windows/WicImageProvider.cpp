#include "platform/windows/WicImageProvider.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstring>
#include <utility>

namespace Runtime {
namespace {

// The apartment this call needs. A thread that was already initialized in another mode is left
// alone, because it is a thread the platform's reader is ready on; a thread this call initialized is
// the one this call owes an uninitialize to.
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
    bool owned = false;
};

}  // namespace

bool DecodeImageWithWic(const unsigned char* data, int size, std::string_view format, DecodedImage& image) {
    // Only the picture whose codec the runtime does not carry is asked for. Every other format is
    // read by a reader of its own, so handing one here would be asking the machine to answer a
    // question the program can answer itself.
    if (data == nullptr || size < 1 || (format != "avif" && format != "avis")) {
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
    image.frames.clear();
    image.frames.push_back(std::move(pixels));
    return true;
}

void RegisterWicImageProvider() {
    SetImageFormatProvider(&DecodeImageWithWic);
}

}  // namespace Runtime
