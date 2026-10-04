# Compiler pipeline

```text
Source file
    |
    v
ReadSourceFile          UTF-8, optional BOM
    |
    v
XmlLexer                tokens with line and column
    |
    v
XmlParser               XML AST, syntax diagnostics UI000
    |
    v
UiCompiler::Lower       catalog lookup, type check, unique ids
    |
    v
IrNode tree             only when there are no failures
    |
    v
Instantiate             Element tree
    |
    v
Measure / Arrange / Paint
```

`CompileUi` and `CompileUiFile` are the entry points. `Ok()` is true only when a root exists and no diagnostic has severity failure.

The runtime never calls the lexer. `Hello` and `myui run` compile before creating the device, then present frames from the live tree.

Optimization in this milestone is the lowering itself: typed lengths, a component catalog, and a single instantiation. Constant folding, style resolution, and partial recompilation are later milestones. A small edit today still recompiles the file and builds a new tree at process start. Inside one run, `Invalidate` only measures and draws again.

`myui check` prints diagnostics and returns 1 on failure. `myui inspect` prints `FormatIr` after a successful compile.
