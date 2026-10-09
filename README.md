# Runtime

Runtime is a Windows-native C++ UI platform. XML is the UI source. A compiler turns that XML into a typed intermediate representation, and a DirectX runtime instantiates, lays out, and draws it.

XML is not parsed in the frame loop.

## Milestone 1

This repository currently proves the first vertical slice:

```text
XML -> parser -> typed UI IR -> runtime -> Win32 window -> DirectX -> button click -> C++ handler
```

Supported elements: `window`, `column`, `text`, `button`.

## Build

The project targets Windows 10 or later and Visual Studio 2022 with the Windows SDK. Open a Developer Command Prompt and run:

```bat
cmake -S . -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs:

```text
build\Hello.exe
build\RoboUIDesigner.exe
build\myui.exe
build\RuntimeTests.exe
build\RuntimeBenchmarks.exe
```

## Run

```bat
build\Hello.exe
build\RoboUIDesigner.exe examples\Hello\Hello.xml
build\myui.exe check examples\Hello\Hello.xml
build\myui.exe inspect examples\Hello\Hello.xml
build\myui.exe run examples\Hello\Hello.xml
build\RuntimeTests.exe
build\RuntimeBenchmarks.exe
ctest --test-dir build --output-on-failure
```

`Hello.exe` registers a C++ `hello` handler. The button calls it and the status text changes. Press Esc to close the window.

`RoboUIDesigner.exe` edits that same XML. The canvas, layers, inspector, and XML editor share one document, and the canvas preview is the compiled runtime. Ctrl+S saves. Middle-drag pans, the wheel zooms, and the corner handle rewrites `width` and `height`.

`Hello.exe --self-test` creates the window, lays the button out, synthesizes a click, and exits after one presented frame.

## CLI

```text
myui check <file>
myui run <file>
myui inspect <file>
```

`new`, `build`, `format`, `test`, `package`, and `profile` are reserved and report that they arrive in a later milestone.

## Docs

- [Architecture](docs/Architecture.md)
- [Module graph](docs/ModuleGraph.md)
- [Repository structure](docs/RepositoryStructure.md)
- [XML language](docs/XmlLanguage.md)
- [Type system](docs/TypeSystem.md)
- [UI IR](docs/UiIr.md)
- [Runtime object model](docs/RuntimeObjectModel.md)
- [Components](docs/Components.md)
- [Designer](docs/DesignerArchitecture.md)
- [Compiler pipeline](docs/CompilerPipeline.md)
- [Roadmap](docs/MilestoneRoadmap.md)
- [Technology choices](docs/TechnologyChoices.md)
- [Vertical slice](docs/VerticalSlice.md)

## Naming

Source filenames are PascalCase. Functions, classes, and structs are PascalCase. Variables are camelCase. Constants are CAPITAL_CASE.
