# Runtime object model

```text
Element
├── WindowElement
├── Column
├── TextBlock
└── Button
```

`Element` owns children, id, width, height, arranged bounds, and the last measured size. It does not own painting resources or window handles.

Subclasses implement `Measure`, and the ones that draw implement `Paint` and `HitTest`. `Button` adds hover, pressed, enabled, and an activate callback. `TextBlock` stores the string that C++ can replace.

## Layout

`Application` calls `Measure` with the client size in device-independent pixels, then `Arrange` with that rectangle.

- `fill` uses the available size.
- `fit` uses the measured content size.
- pixels and percents resolve against the available size.
- A `fill` or percent height inside a `fit` column uses the content size, because the parent height is not definite.
- `column` stacks children vertically, applies padding and spacing, and aligns on the cross axis.
- `stretch` widens a `fit` child to the inner width.
- The window element gives its children the client width and stacks their measured heights from the top.

Text measurement goes through `TextMeasurer`. Production code uses DirectWrite. Tests and the layout benchmark use a deterministic stand-in.

## Input

`PointerInput` hit-tests from front to back. A mouse up on the same element that received mouse down calls `Activate`. An enabled button runs its click callback. Disabled buttons do not.

## Invalidation

`UiContext::Invalidate` marks the frame dirty. The next loop measures, arranges, and presents again. Unchanged nodes stay in place.

## Coordinates

Win32 mouse positions are physical pixels. Layout and Direct2D use device-independent pixels. `Dx11Renderer::SetDpi` sets the Direct2D DPI so a layout unit matches a DIP on the swap chain.
