# Repository structure

```text
Runtime/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── docs/
├── engine/
│   ├── core/
│   ├── math/
│   └── types/
├── xml/
│   ├── ast/
│   ├── diagnostics/
│   ├── lexer/
│   ├── parser/
│   └── serializer/
├── compiler/
│   ├── ir/
│   ├── semantic/
│   └── types/
├── rendering/
│   ├── renderer/
│   └── text/
├── ui/
│   ├── element/
│   ├── factory/
│   └── input/
├── components/
│   ├── basic/
│   └── layout/
├── platform/
│   └── windows/
├── app/
├── examples/
│   └── Hello/
├── tools/
│   └── cli/
├── tests/
└── benchmarks/
```

Later milestones add `designer/`, richer `components/`, `platform` backends besides Windows, and the remaining `myui` commands. Those directories are intentionally absent until they contain a working program.

Filenames are PascalCase. The CMake file keeps the name CMake requires.
