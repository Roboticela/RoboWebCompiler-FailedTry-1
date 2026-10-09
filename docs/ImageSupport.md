# Image support

The image subsystem turns a file a document names into the pixels the renderer draws. It is written
in portable C++ and lives in `rendering/image/`; nothing in it calls into the operating system.

## Formats

| Format | Read by | Notes |
|---|---|---|
| PNG | `StillImage.cpp` | All bit depths, palette and truecolour, Adam7 interlacing, `tRNS`, 16-bit samples. Every frame of an APNG, with per-frame delays, blend and disposal so an animation plays by being drawn. |
| JPEG | `StillImage.cpp` | Baseline and extended sequential, and progressive: every scan of a file is applied to the same block coefficients, so spectral selection and successive approximation come out as the encoder meant them. Greyscale and YCbCr, restart markers, 4:4:4, 4:2:2 and 4:2:0. |
| GIF | `StillImage.cpp` | Every frame, with per-frame delays, transparency and disposal so an animation plays by being drawn. |
| WebP | `WebpImage.cpp`, `Vp8LImage.cpp`, `Vp8Image.cpp` | Both kinds of picture and the extended container: a lossless one, a lossy one, or a canvas that carries either with an alpha plane beside it. Animation (`ANIM`/`ANMF`) composites each frame onto the canvas with its own delay, blend and disposal. |
| SVG | `SvgImage.cpp` | Rasterised by the runtime's own path renderer, so a vector icon is drawn without a browser. |
| BMP | `BmpImage.cpp` | 1, 4, 8-bit palette and 16, 24, 32-bit; `BI_RGB`, `BI_BITFIELDS` and alpha bitfields; top-down and bottom-up rows. |
| ICO / CUR | `IcoImage.cpp` | The largest picture in the file, whether it is a PNG inside or a bitmap without a file header. The cursor hotspot is ignored. |
| TIFF | `TiffImage.cpp` | Either byte order, the first picture of a file, and both strips and tiles: none, PackBits, LZW with the early-change rule and Deflate. 1, 4, 8 and 16-bit samples; greyscale, palette and RGB; the horizontal predictor; alpha and its meaning. |
| AVIF | `AvifImage.cpp` | The container is read here, so the format and the stated size are the runtime's own. The pixels need an AV1 decoder, which the runtime does not carry. |
| AVI / IVF | `VideoStream.cpp` | Containers of moving pictures whose frames are whole pictures: Motion JPEG in an AVI, whole VP8 pictures in an IVF. Read where the frames lie rather than whole, one frame at a time. |

A WebP's lossless pictures are read by `Vp8LImage.cpp` and its lossy ones by `Vp8Image.cpp`; both are
the container's picture and not a wrapper around anybody else's decoder. Every stage the formats
name is here: the entropy-coded transforms and colour cache of a lossless picture, and the
prediction, quantisation, inverse transform and loop filter of a lossy one. A lossy picture's colour
is stored at half the width and height of the picture, so `Vp8Image.cpp` also spreads each colour
sample back over the square of pixels it belongs to, with the sample nearest a pixel taken at three
times the weight of the one beside it, and converts the result with the fixed-point sums the format
itself names.

An alpha plane a lossy picture carries (`ALPH`) is stored either plainly or as a lossless picture
whose green channel holds it, and either way it is stored as differences from a guess at each pixel.
All four guesses are read here, including the rule that the first row of a plane has nothing above it
and so falls back to the guess that follows the row, and the seven-bit form the encoder writes when
it has reduced the levels is spread back over the range it came from.

The runtime's own pixels were checked against a reference decoder that reads the same files: every
lossless picture in a corpus of 313 real WebP files comes out byte for byte, and every lossy one
comes out byte for byte but for a handful of pixels in some files, where a value differs by at most
five parts in 255 around one macroblock. Pictures written by the reference encoder and read back
through the alpha plane agree with it exactly, for every compression and filter the format offers.

A TIFF is the opposite kind of format: many files, each naming the few ways it is laid out. The
reader follows the chain of directories and reads the first picture, which is the one every other
reader means by a TIFF, and gathers from its tags what the samples are and how they are packed. The
compressions are the ones the format is actually found with — a plain run, PackBits runs, LZW, and
Deflate, which reuses the same inflater as a PNG and so is read as a zlib stream and, failing that,
as a raw one, because files of both kinds exist. LZW follows the early-change rule the format's own
writers use, which is what makes a file written by any of them come out rather than only the ones
written by a reader that guessed the other way. Rows are written either as strips or as square
tiles, in either byte order, and a file may state what each strip or tile holds rather than one rule
for all of them.

The samples are then turned into the one layout the renderer draws: greyscale spread across 0–255
and, at sixteen bits, cut to their top eight, which is what the format's own library does with them
rather than what a viewer's rounding would give. A palette is looked up and its entries widened the
same way. Alpha is honoured once the file has said what it is: if the extra samples count one and
the tag says it is unassociated, the colour is folded into it the way the renderer expects; if it
says the colour is already the colour that is there, the samples are kept as they are.

A TIFF was checked the same way: pictures written by two other libraries, covering every byte order,
every compression above, palette and greyscale and RGB, sixteen-bit samples, tiles and both predictor
settings, come out byte for byte. Where a picture differed it was the other library's rounding of
sixteen-bit samples that differed, not the reading.

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

## Playing a picture

A picture that holds more than one frame is drawn a frame at a time, and which frame is drawn is a
fact about the moment. The `image` element draws the frame the clock has reached, which is what makes
a GIF play by being drawn: there is no state to pause and none to seek. The `video` element draws the
frame its own playhead stands in, so the same picture can be paused, started again, run slower or
faster, seeked to a moment, stopped at its end, or looped back to its beginning, and a click anywhere
on it starts it or stops it. The element carries the playing of a picture and nothing else about it:
it is built on `image`, which is built on the rectangle, so it is a box with corners, a turn, a
fitting and a look by being one rather than by copying any of them.

A picture held whole and a picture read out of a container are two different things to the element,
and which of the two a file is is a fact about the file rather than a flag on the node. A still
picture, a GIF and an animated WebP are read whole and kept, because there are few enough frames of
them and each is small enough that holding them costs nothing. A container of moving pictures is not:
a minute of one is thousands of frames and a frame of a large one is megabytes, which is more than a
document has any right to hold. `VideoStream.cpp` walks the container for where its frames lie and
reads one frame when it is wanted, holding only the frame being drawn. It opens an AVI whose frames
are Motion JPEG and an IVF whose frames are whole VP8 pictures, and it opens them only when every
frame of the file stands on its own — a frame that leans on the ones before it cannot be read out of
the middle of a file, so such a file is refused whole rather than drawn with gaps in it. A file the
container reader does not open is read the way any other picture is, so nothing above it has to know
which of the two happened.

Either way the element is handed the same four things — how many frames there are, how big the
picture is, the moment each frame begins at, and the frame that moment falls in — so a video playing
an AVI is the same video as one playing a GIF. Widening what plays means writing a reader for another
container or another codec, which is the same work as any other format in this document and is costed
the same way, rather than a switch that turns video on.

A container keeps its file open for as long as it stands, because it is an index of where the frames
lie rather than a copy of them. A file that is a container is therefore open in the sense the
operating system means: it can be read by anything else, and it cannot be removed or replaced while
the element holding it is alive.

## Not supported

- **MP4, WebM and MOV, and the H.264, HEVC, VP9 and AV1 frames they carry** — a `video` element
  naming one draws the gap an unreadable picture draws. Only a container whose every frame is a
  whole picture is opened: an AVI of Motion JPEG and an IVF of whole VP8 pictures are read, and a
  container whose frames lean on each other is refused whole.
- **Sound** — nothing here reads an audio track, and a container's sound track is skipped.
- **HEIC / HEIF** — uses HEVC, the same class of problem as AV1.
- **AV1 pictures inside AVIF** — the container is read and the size it states is kept; the pixels
  need the optional decoder above. An animated AVIF names and measures its frames but could only
  ever draw a first one.
- **A later picture in a multi-page TIFF**, the JPEG and CCITT compressions some scanners use, and
  the separated (CMYK) colour some presses use — the file opens and its first picture is drawn.
