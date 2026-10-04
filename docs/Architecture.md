# Architecture

Runtime is a C++20 UI platform for Windows. XML is the declarative UI language. The compiler produces a typed UI intermediate representation. The runtime instantiates that representation, lays it out, and draws it with DirectX. C++ owns application logic.

```text
Visual Designer (later milestone)
        |
        v
      UI XML  <---- XML editor
        |
        v
   Lexer / parser
        |
        v
     XML AST
        |
        v
 Semantic analysis and type check
        |
        v
      UI IR
        |
        v
   Instantiate runtime tree
        |
        v
 Layout -> DirectX renderer -> Win32 window
        ^
        |
   C++ event handlers
```

## Rules

1. XML is the UI source. The future Designer edits that XML through a typed in-memory model and serializes it back without a second design format.
2. The frame loop consumes the compiled runtime tree. It does not parse XML.
3. UI components talk to a `Renderer` interface. The only backend in this repository is Direct3D 11 with Direct2D and DirectWrite.
4. The build is Windows-only until another backend exists.
5. Solid fills, rounded rectangles, clipping, and text are the drawing primitives. Shadows and gradients are excluded.

## Layers

| Layer | Responsibility |
| --- | --- |
| `engine` | Math, color, length, alignment, logging, source locations |
| `xml` | Lexer, parser, AST, serializer, diagnostics |
| `compiler` | Catalog, type parsing, semantic analysis, UI IR |
| `ui` | Elements, input, factory |
| `components` | `window`, `column`, `text`, `button` |
| `rendering` | Backend-neutral renderer and text measurement |
| `platform/windows` | Win32 window and DirectX renderer |
| `app` | Composition: compile, instantiate, run |
| `designer` | Specified, not implemented |
| `tools` | `myui` CLI |

## Ownership

- `CompileResult` owns the IR tree by value.
- `Application::Run` owns the `Element` tree with `std::unique_ptr`.
- Parents own children. Parent pointers are non-owning.
- Click callbacks are `std::function<void()>` copied onto the button. They may call back into `UiContext`, which is owned by `Application::Run` and outlives the tree.
- DirectX COM objects are owned by `Microsoft::WRL::ComPtr` inside `Dx11Renderer`.

## Frame

On startup the XML file is compiled once. Each invalidation measures and arranges the tree, then `Dx11Renderer` draws it and presents the DXGI swap chain. Mouse input hit-tests the arranged bounds. A click invokes the C++ handler registered for `onClick`. The handler can change text and invalidate layout. The rest of the tree stays allocated.
