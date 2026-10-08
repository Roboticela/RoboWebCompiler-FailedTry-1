# Image support

The image subsystem turns a file a document names into the pixels the renderer draws. It is written
in portable C++ and lives in `rendering/image/`; nothing in it calls into the operating system.

## Formats

| Format | Read by | Notes |
|---|---|---|
| PNG | `StillImage.cpp` | All bit depths, palette and truecolour, Adam7 interlacing. The first frame of an APNG; the animation chunks are ignored. |
| JPEG | `StillImage.cpp` | Baseline and extended sequential, greyscale and YCbCr, restart markers. Progressive JPEG is refused. |
| GIF | `StillImage.cpp` | Every frame, with per-frame delays, transparency and disposal so an animation plays by being drawn. |
| WebP | `WebpImage.cpp` | A single lossy (VP8) frame. Lossless and animated WebP are refused. |
| SVG | `SvgImage.cpp` | Rasterised by the runtime's own path renderer, so a vector icon is drawn without a browser. |
| BMP | `BmpImage.cpp` | 1, 4, 8-bit palette and 16, 24, 32-bit; `BI_RGB`, `BI_BITFIELDS` and alpha bitfields; top-down and bottom-up rows. |
| ICO / CUR | `IcoImage.cpp` | The largest picture in the file, whether it is a PNG inside or a bitmap without a file header. The cursor hotspot is ignored. |
| AVIF | `AvifImage.cpp` | The container is read here, so the format and the stated size are the runtime's own. The pixels need an AV1 decoder, which the runtime does not carry. |

## The one optional decoder

An AVIF is an AV1 picture, and an AV1 decoder (CDF entropy coding, directional intra prediction,
DCT/ADST transforms, loop restoration) is an order of magnitude larger than every reader here put
together. The codec is not part of the runtime, so `DecodeAvifImage` reads the container and leaves
the frames empty.

A host that has a reader for it registers one:

```cpp
Runtime::SetImageFormatProvider(&Decoder);
// or, on Windows, the reader the platform ships beside it:
Runtime::RegisterWicImageProvider();
```

The provider is offered a file only after every reader written here has passed on it, and only for
the formats it claims. `platform/windows/WicImageProvider.cpp` claims AVIF and reaches the platform
reader, which delegates AV1 to the "AV1 Video Extension". Where that extension is installed the
pixels arrive; where it is not the file is refused.

The point of the slot is that the runtime's own behaviour is a property of the program rather than
of the machine: with no provider registered, an AVIF is refused on every operating system in exactly
the same way, and is measured at exactly the same size, because the container is read here.

## A file that cannot be read

A picture whose pixels cannot be produced still lays out as the picture it holds when its container
states a size. `DecodedImage::declaredWidth` and `declaredHeight` carry that statement, and
`ImageElement::Intrinsic` falls back to it before falling back to the unnamed placeholder box. An
AVIF on a machine with no AV1 codec therefore leaves a gap of the right size and shape rather than a
box of a fixed size.

## Not supported

- **HEIC / HEIF** — uses HEVC, the same class of problem as AV1.
- **TIFF** — a family of compressions and layouts rather than one format.
- **Progressive JPEG**, **lossless and animated WebP**, **animated PNG**, **APNG**, **animated
  AVIF** (the container is read and the format is reported; only the first frame would be drawn).
