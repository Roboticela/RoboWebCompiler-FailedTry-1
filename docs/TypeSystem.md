# Type system

Typed values replace raw attribute strings before the runtime sees a node.

| Type | Milestone 1 form | Example |
| --- | --- | --- |
| String | UTF-8 text | `title="Runtime"` |
| Bool | `true` or `false` | `enabled="false"` |
| Color | `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, or a named color | `color="#111111"` |
| Length | pixels, percent, `fill`, `fit` | `width="120px"` |
| FontSize | pixel size | `fontSize="32"` |
| Alignment | `start`, `center`, `end`, `stretch` | `alignment="center"` |
| EventHandler | identifier | `onClick="hello"` |

`Int` and `Float` exist in the type enumeration for later numeric properties. No Milestone 1 property uses them directly. `FontSize` is stored as a number of pixels.

Named colors: `black`, `white`, `red`, `green`, `blue`, `gray`, `grey`, `transparent`.

## Checking

`ComponentCatalog` lists every element and the type of each property. The compiler:

1. Rejects unknown elements and properties, with a closest-name suggestion when one is close.
2. Parses the attribute into the declared type.
3. Rejects a negative pixel length or font size.
4. Requires `window` width and height to be pixel lengths.
5. Records ids and rejects duplicates.
6. Rejects children on `text` and `button`.

There are no implicit conversions from an arbitrary string into a length or color. `120` becomes a length because the length grammar accepts a bare number. `hello` stays a string and produces `UI014`.

## Defaults

Defaults are applied when the runtime object is created, not written back into the IR.

- Column width is `fill`, height is `fit`, alignment is `start`, padding and spacing are `0`.
- Button and text sizes are `fit`.
- Button corner radius is `6` pixels.
- Button background is `#2563EB`.
- Text color is `#111111`.
- Font size is `16` for text and `14` for buttons.

## Later types

`Margin`, `Padding` as a four-edge box, `Font`, `Duration`, `Resource`, `ComponentReference`, and `Binding` are part of the language direction. They are not parsed as distinct successful types yet. A `{...}` binding is recognized and rejected so the syntax is reserved.
