# Multi-file documents

A document does not have to be one file. A screen can borrow its definitions from a library, inline a
part that lives somewhere else, and link to another screen, and the files it is made of are read as
one document. Nothing about the runtime changes: it still compiles one tree, measures it, arranges
it, and draws it. What changes is how many files that tree was written across.

## The two ways a file names another file

Both name the file they read with `src`, resolved **beside the file that wrote it** rather than beside
wherever the program was started from, so a document that ships with its parts reads them wherever it
is run from.

| Element | What it does |
|---|---|
| `import` | Borrows the definitions of another document: its components, sets, tokens, styles, states, and animations. Nothing of it is drawn. |
| `embed` | Inlines the content of another document at the place it stands, so the tree that is drawn is one tree made of several files. |

```xml
<screens>
    <import src="Theme.xml"/>
    <window id="Main" width="560" height="380" background="$paper">
        <embed id="header" src="Parts/Header.xml" width="fill" height="64"/>
    </window>
</screens>
```

## Libraries

A file written to be borrowed from has a `library` root. A library holds definitions and nothing else,
which is what makes it possible to read a library on its own as well as to import it:

```xml
<library>
    <tokens>
        <color name="brand" value="#0E7AFF"/>
    </tokens>
    <style name="Panel" background="#FFFFFF" radius="12" padding="16"/>
    <component name="Badge" width="80" height="28" background="$brand" radius="14">
        <text id="badgeLabel" text="New" fontSize="13" textColor="#FFFFFF"/>
    </component>
</library>
```

`myui check Theme.xml` reads the library by itself, so a library is checked without a screen wrapped
around it. A library that holds something a definition cannot be is reported (UI108) rather than
quietly kept.

An imported file may also be a `window`, a `board`, or a `screens`. Only the definitions at the top of
it are borrowed; the rest of it is not, because a screen in another file is not something a document
draws where it stands.

## What an import does and does not do

- It makes the borrowed names **usable**: a screen places a copy of an imported component, writes an
  imported style, and reads an imported token, exactly as if the definition had been written here.
- It does not place anything. A borrowed definition is in the tree a copy is resolved against and is
  not built into any screen, so importing a library of twenty components does not draw twenty boxes.
- A library named twice is one library rather than two, so a name that repeats is a guard rather than
  a mistake. Two documents reaching the same library by different paths still offer its definitions
  once.

## Embedding

An `embed` inlines **the content of the named document**: the children of its root. A file whose root
is a `window` holds a screen's content, and embedding it puts that content where the embed stands. The
embed node stays in the tree and keeps the name of the file it came from, so a document built out of a
folder can say which file each part of it was read from.

An embed that names **no file** is a place waiting for one: it holds what it declares and inlines
nothing, which is what lets a part be designed before the file behind it exists.

## Links

A link is a frame that goes somewhere when it is clicked. It names the place it goes and the program
decides what going there means:

```xml
<link id="docsLink" href="https://example.com/docs" text="Open the docs"/>
<link id="settingsLink" to="Settings" text="Settings"/>
```

`href` is a name the system opens; `to` is a screen of the same document. A link that names neither is
refused (UI109), and a link whose `to` names a screen the document does not hold is refused where it
was written (UI110) rather than doing nothing when it is clicked.

## Reading a document that is several files

Every file that was read is kept, so a diagnostic is shown beside the file it was found in rather than
beside the file the program was started with:

```text
D:\app\Parts\Header.xml:3:20
error UI014: Property 'height' expects Length. Received String.
```

`myui files` prints the files a document is made of, in the order they were read, with the entry file
first — the answer to "why is this element here" and the list to watch when re-reading a screen:

```text
0 D:\app\MultiFile.xml
1 D:\app\Theme.xml
2 D:\app\Parts\Header.xml
```

A cycle — a document that imports a document that imports it back — has no first file to read, so it is
refused (UI106). A chain nested more deeply than 32 documents is refused as well (UI022).

## Splitting the program

The same split applies to the C++, and nothing new is needed for it: `Application::Run` takes the map
of handlers, so a program answers its documents with as many files of handlers as it is written in. The
example in `examples/MultiFile` is one screen across four XML files and the program behind it across
three C++ files.

A node that names a handler with `onClick` is wired to that handler when the tree is built, and it
answers the mouse and the keyboard through the same event, so a click and Enter do the same thing.
