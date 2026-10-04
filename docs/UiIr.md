# UI intermediate representation

The IR is the compiler output and the runtime input. It is a tree of `IrNode` values.

```text
window
  column
    text
    button
```

Each node stores:

- `kind`: `WINDOW`, `COLUMN`, `TEXT`, or `BUTTON`
- `name`: the XML element name
- `id`: optional
- `location`: where the element began
- `properties`: typed `UiValue` entries
- `children`

A `UiValue` carries a `TypeKind` and the active payload: text, bool, number, color, length, or alignment. A button width is a `Length`, not the string `"120px"`.

`FormatIr` prints the tree for `myui inspect`. `Instantiate` walks the IR once and builds the live `Element` tree. After that, rendering uses the live tree.

The IR does not store raw XML text. Source locations remain so a later Designer and diagnostic tool can map a node back to the file. The serializer that writes XML uses the XML AST, which still has attribute order. A Designer edit path is: parse to AST, edit, serialize. The compiler path is: parse to AST, lower to IR, instantiate.
