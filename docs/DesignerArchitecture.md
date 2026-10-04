# Designer architecture

The Designer is a later milestone. Its source of truth is the same XML the compiler reads.

```text
Canvas, hierarchy, inspector, XML editor
        |
        v
  XML AST in memory
        |
        +--> SerializeXml --> UI XML on disk
        |
        +--> CompileUi --> UI IR --> same runtime as the app
```

There is no project format that the Designer owns and then exports to XML. `XmlSerializer` already writes an AST back to XML so an editor can be lossless for elements and attributes. Comment preservation is still missing and has to land before the Designer can claim full round-trip safety.

Selection is one node identity shared by the hierarchy, the canvas, and the XML span stored on `SourceLocation`.

The inspector edits typed properties from `ComponentCatalog`. Changing a width writes a length attribute, then the preview compiles the affected tree and reuses `Application`'s runtime. The preview is not a second renderer.

Panels specified for that milestone:

- Canvas with pan, zoom, select, move, resize, align, snap
- Hierarchy
- Property inspector
- XML editor with diagnostics from this compiler
- Preview at phone, tablet, desktop, and custom sizes

Animation timeline, state editor, and interaction editor come after the canvas and the XML editor share one AST.

C++ remains the language for application logic. The Designer may bind `onClick` to a handler name. It does not replace the handler's implementation.
