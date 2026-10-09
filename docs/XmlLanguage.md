# XML language

Milestone 1 accepts a single root `window` and the elements `column`, `text`, and `button`.

```xml
<window width="800" height="600" title="Runtime">
    <column padding="32" spacing="16" alignment="center">
        <text id="status" text="Click the button" fontSize="16" color="#444444"/>
        <button id="helloButton" text="Hello" width="160px" height="44px" onClick="hello"/>
    </column>
</window>
```

## Grammar

The lexer accepts elements, attributes, quoted strings, comments, and an optional XML declaration. Comments and the declaration are skipped. The serializer writes elements and attributes and does not yet round-trip comments.

```text
document    = element
element     = '<' name attribute* '/>'
            | '<' name attribute* '>' element* '</' name '>'
attribute   = name '=' string
```

Strings use `"` or `'`. Character references `&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;`, and numeric references are decoded into the attribute value.

## Elements

| Element | Children | Properties |
| --- | --- | --- |
| `window` | yes | `id`, `width`, `height`, `title` |
| `column` | yes | `id`, `width`, `height`, `padding`, `spacing`, `alignment` |
| `text` | no | `id`, `width`, `height`, `text`, `value`, `fontSize`, `color` |
| `button` | no | `id`, `width`, `height`, `text`, `onClick`, `background`, `enabled`, `cornerRadius` |

`value` on `text` is an alias of `text`. Setting both is an error.

`window` `width` and `height` are pixel lengths. They size the Win32 client area in device-independent pixels. `title` is the window caption.

## Lengths

```text
120
120px
50%
fill
fit
```

Bare numbers are pixels. `column` defaults to `width="fill"` and `height="fit"`. `text` and `button` default to `fit`.

## Events

`onClick` is a C++ handler name: a letter or `_`, then letters, digits, or `_`. The application registers that name before the window runs. `myui run` has no registrations, so a click logs the handler name.

A value that starts with `{` is a binding expression. The compiler accepts the syntax only far enough to reject it with `UI040`. Bindings are a later milestone.

## Diagnostics

Every diagnostic has a file, line, column, code, and message. Unknown names include a suggestion when the edit distance is small.

```text
memory.xml:1:17
error UI014: Property 'width' expects Length. Received String.
<window><button width="hello"/></window>
                ^^^^^
```

## Several files

A document may be written across several files. `import` borrows the definitions of another document;
`embed` inlines the content of one where it stands; a file written to be borrowed from has a `library`
root; and `link` is a frame that goes somewhere when it is clicked. Each name is resolved beside the
file that wrote it, and every diagnostic is shown beside the file it was found in. See
[Multi-file documents](MultiFile.md) for the whole of it.

| Code | Meaning |
| --- | --- |
| UI000 | Syntax error |
| UI001 | Unknown component |
| UI002 | Duplicate id |
| UI007 | Unreadable file |
| UI010 | Root is not `window` |
| UI014 | Type mismatch |
| UI016 | Value out of range |
| UI020 | Unexpected child |
| UI021 | Duplicate property |
| UI022 | Tree deeper than 64 |
| UI031 | Event handler name is not an identifier |
| UI040 | Binding expression |
| UI102 | Unknown property |
| UI105 | Child of a copy names nothing the definition offers (warning) |
| UI106 | A document imports a document that imports it back |
| UI107 | A document named by an import is not a document that can be borrowed from |
| UI108 | A library holds something a definition cannot be (warning) |
| UI109 | A link names neither `href` nor `to` |
| UI110 | A link names a screen the document does not hold |
