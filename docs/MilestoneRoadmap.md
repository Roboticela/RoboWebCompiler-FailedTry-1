# Milestone roadmap

Each milestone keeps a program that builds, runs, and has tests before the next one starts.

## Milestone 1 — current

CMake, XML lexer and parser, diagnostics, type checker, UI IR, Win32 window, Direct3D 11 / Direct2D / DirectWrite renderer, text, column, button, mouse click, C++ handler, `myui check`, `myui run`, `myui inspect`, unit tests, compile and layout benchmarks.

## Milestone 2

Row, text field, richer properties, focus, keyboard text input, more diagnostics.

## Milestone 3

IR optimizer, compiled UI package, load the package without reparsing XML at startup.

## Milestone 4

Styles, themes, design tokens, bindings, animations. `{...}` stops being a hard error and becomes a typed binding.

## Milestone 5

Designer: canvas, hierarchy, inspector, XML editor, runtime preview. Serializer gains comment and whitespace fidelity first.

## Milestone 6

Drag and drop, constraints, snapping, animation timeline, visual states.

## Milestone 7

Templates, custom component registration, hot reload of a running preview, assets.

## Milestone 8

Accessibility tree, more controls, profiler, packaging. `myui build`, `myui test`, `myui package`, and `myui profile` become real commands.

The platform layer can gain more backends later. Those backends implement `Renderer` and a window. They do not change XML or the IR.
