# Components

The component system is how one piece of a document is written once and placed many times. It is six
elements of the XML language and six nodes of the runtime tree: four for the parts a document is made
of, and two for the documents a document is made of.

| Element | Node | What it is |
|---|---|---|
| `component` | `COMPONENT` | The definition of something that can be placed more than once. |
| `componentSet` | `COMPONENTSET` | The variants of one component gathered under a single name. |
| `instance` | `INSTANCE` | A placed copy of a definition. |
| `slot` | `SLOT` | A place inside a definition that a copy fills with content of its own. |
| `embed` | `EMBED` | Another document inlined where it stands. |
| `link` | `LINK` | A frame that goes somewhere when it is clicked. |

Each of the four is a frame, so each holds children, lays them out, and carries a background, a
border, padding, a radius, and a turn. None of them was written from scratch: the four nodes are the
frame the rest of the language is built out of, and each one adds only the values the component
system names.

## Definitions

A definition is a node that answers to a name and holds the content it draws:

```xml
<component name="Badge" variant="Primary" width="90" height="30" layout="horizontal" align="center">
    <text name="Label" text="Ok"/>
</component>
```

A definition that belongs to a set also names the variant it draws. A definition with no body is
refused, because a definition is the content it holds.

## Sets

A set gathers the variants of one component under a single name:

```xml
<componentSet name="Button" layout="horizontal" gap="16">
    <component name="Button" variant="Primary" width="140" height="40"><text text="Primary"/></component>
    <component name="Button" variant="Danger"  width="140" height="40"><text text="Danger"/></component>
</componentSet>
```

A copy that names a variant is built from the definition that draws that variant. A copy that names
no variant, or names one no definition draws, is built from the first definition that answers to the
name rather than from nothing.

## Copies

A copy names the definition it was placed from and, when its definition is one of several variants,
the variant it shows:

```xml
<instance component="Button" variant="Danger" width="140" height="40"/>
```

The copy holds the content of its master, resolved when the document is built, so a copy is a real
subtree rather than a layer that reaches into the definition. A copy whose definition the document
does not hold is left unresolved and draws a marked placeholder, so a document that points at a
missing master is seen on the canvas instead of being silently blank.

A copy that has been detached (`detach="true"`) is a copy that has stopped following its master.

## Overrides

A copy differs from its master by naming the children it replaces. The child of the copy is matched
to the child of the master by either name the master's child answers to, its `id` or its `name`:

```xml
<instance component="Chip" width="120" height="32">
    <text name="Label" text="Save"/>
</instance>
```

The master keeps reading the way it was written; only the copy changes. `InstanceElement::OverrideCount`
says how many of the master's children a copy replaced.

A child may be named by the `id` its layer carries as well as by its `name`:

```xml
<component name="Tag"><text id="tagLabel" text="Ok"/></component>

<instance component="Tag">
    <text id="tagLabel" text="Renamed"/>
</instance>
```

A child of a copy stands in place of a layer of its master, so the `id` it carries names that layer
rather than making a second layer of its own. It is therefore allowed to repeat the id it replaces,
which is what lets a copy be written against the same ids the definition was written with. An id
written twice outside a copy is still the same second name for one layer, and is refused.

Matching reaches a layer wherever the definition keeps it, not only a child at the top. A definition
built out of boxes the way a screen is built can still have a layer deep inside it replaced by a copy.

A child of a copy that names neither a layer of the definition nor a place of it cannot be placed by
anything, so it is reported rather than dropped in silence. What is reported does not stop the
document: the rest of the copy is still drawn.

## Placing by name

A definition can also be placed by writing its name as an element, without an `instance` around it.
The usage names the variant it draws the same way a copy does, and is expanded from the definition
that draws that variant:

```xml
<componentSet name="Pill" layout="horizontal" gap="8">
    <component name="Pill" variant="Primary" width="140" height="40"><text text="A"/></component>
    <component name="Pill" variant="Danger"  width="140" height="40"><text text="B"/></component>
</componentSet>

<Pill variant="Danger" width="140" height="40"/>
<Pill width="140" height="40"/>
```

The variants of one component stand beside each other rather than replacing one another: a usage
that names a variant is drawn from the definition that draws it, and a usage that names none is drawn
from the first definition that answered to the name. A definition written twice, or a variant of one
name written twice, is refused, because the second can never be placed.

## Description

A definition can say what it is for. That description is drawn as the tip the layer shows while it is
hovered, the same way every other description in the document is drawn:

```xml
<component name="Note" description="Shown in the layer list"><text text="Note"/></component>
```

## Slots

A slot is the place inside a definition where a copy puts content of its own. A copy sends a child
to a slot by naming the slot on the child:

```xml
<component name="Panel" layout="vertical">
    <text name="Title" text="Title"/>
    <slot name="body" text="Drop content"/>
</component>

<instance component="Panel" width="200" height="100">
    <frame slot="body" width="200" height="80" background="#010203"/>
</instance>
```

A slot that no copy filled keeps its fallback, which is drawn as a label inside the slot's outline
rather than being laid out as content. A slot named `content`, or left unnamed, takes the children a
copy sent without naming a slot at all, which is how a definition offers a plain box of content as
well as the places it names.

`slot` is accepted on any component, not only on the ones a definition happens to declare: which
places a definition offers is only known once the definition is placed, so the name is read as the
part of the component language it is.

## Embed

An embed is another document inlined where the embed stands. It is a frame in every way a document
cares about, so what it holds is laid out and drawn exactly as it would be anywhere else; what makes
it an embed is the file it was read from. The name is kept, so a document built out of a folder of
files can say which file each part of it came from.

```xml
<embed id="header" src="Parts/Header.xml" width="fill" height="64"/>
```

An embed that names no file is a place waiting for one: it holds what it declares and inlines nothing,
which is what lets a part be designed before the file behind it exists. An embed is drawn with a teal
mark in its corner, which tells it apart from the definitions around it, and its description is drawn
as the hover tip every other description is drawn as. The mark can be turned off with `badge="false"`.

## Link

A link is a frame that goes somewhere when it is clicked. It is a frame in every way a document cares
about, so a link that holds content lays that content out; a link that holds nothing draws its own
`text` label. What makes it a link is the place it names:

```xml
<link id="docsLink" href="https://example.com/docs" text="Open the docs"/>
<link id="settingsLink" to="Settings" text="Settings"/>
```

`href` is a name the system opens; `to` is a screen of the same document. The runtime does not decide
what going there means: it hands the names to the handler the program gave the factory, so a document
says where a link goes and the program says what going there does. A link with no handler is still a
link -- drawn, and doing nothing when it is clicked -- which is what lets a document be shown before
the program behind it exists. A link asks for the hand cursor, and the mouse and Enter both activate it.

## Marks

A definition and a set are drawn with a mark in their corner, so a master is told apart from the
frame it was built out of. A set is drawn with two overlapping marks. A copy draws exactly what its
master draws, with no mark of its own, because a copy is the finished thing. An unresolved copy
draws an outlined placeholder.

Any mark can be turned off with `badge="false"`.
