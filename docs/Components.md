# Components

The component system is how one piece of a document is written once and placed many times. It is
four elements of the XML language and four nodes of the runtime tree.

| Element | Node | What it is |
|---|---|---|
| `component` | `COMPONENT` | The definition of something that can be placed more than once. |
| `componentSet` | `COMPONENTSET` | The variants of one component gathered under a single name. |
| `instance` | `INSTANCE` | A placed copy of a definition. |
| `slot` | `SLOT` | A place inside a definition that a copy fills with content of its own. |

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
to the child of the master by the `name` the two of them carry:

```xml
<instance component="Chip" width="120" height="32">
    <text name="Label" text="Save"/>
</instance>
```

The master keeps reading the way it was written; only the copy changes. `InstanceElement::OverrideCount`
says how many of the master's children a copy replaced. Matching is by name rather than by id,
because a master and its copies are lowered in the same document and an id may only be written once.

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

## Marks

A definition and a set are drawn with a mark in their corner, so a master is told apart from the
frame it was built out of. A set is drawn with two overlapping marks. A copy draws exactly what its
master draws, with no mark of its own, because a copy is the finished thing. An unresolved copy
draws an outlined placeholder.

Any mark can be turned off with `badge="false"`.
