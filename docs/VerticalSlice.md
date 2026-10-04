# First vertical slice

`examples/Hello/Hello.xml` is the slice:

```xml
<window width="800" height="600" title="Runtime">
    <column padding="32" spacing="16" alignment="center">
        <text id="title" text="Welcome" fontSize="32" color="#111111"/>
        <text id="status" text="Click the button" fontSize="16" color="#444444"/>
        <button id="helloButton" text="Hello" width="160px" height="44px" onClick="hello" background="#2563EB"/>
    </column>
</window>
```

`examples/Hello/Main.cpp` registers `hello`. The handler sets the status text to `Hello from C++` and invalidates the frame.

What this proves:

- The XML file is compiled before the window loop.
- `width="160px"` is a pixel length in the IR.
- `width="hello"` fails with `UI014` and does not open a window.
- The button is centered by the column.
- A click, including the synthetic self-test click, reaches C++.
- Drawing goes through Direct3D 11, Direct2D, and DirectWrite.

Run the window:

```bat
build\Hello.exe
```

Run the headless-enough proof (a real window is created, one frame is presented, then the process exits):

```bat
build\Hello.exe --self-test
```

Measure compile and layout:

```bat
build\RuntimeBenchmarks.exe
```

A Release run on this development machine measured about 13.1 microseconds per XML compile (1000 iterations) and 0.072 microseconds per layout pass (10000 iterations) for the small benchmark tree. `Hello.exe --self-test` presented a DirectX frame and invoked the C++ click handler. Frame time is not claimed from the layout number.
