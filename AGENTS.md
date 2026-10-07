The filenames must be PascalCase. The functions, classes, and structs must be PascalCase. Variable names must be camelCase. Constants must be CAPITAL_CASE.

IMPORTANT RULE: Every component must be made of others components if possible instead of everything from scratch.

# 2. Your core component/node system

Don't make every visual thing a separate React component.

Instead, create a universal **Node** model.

Something like:

```text
Node
├── Frame [Done]
├── Group
├── Rectangle
├── Ellipse
├── Line
├── Polygon
├── Star
├── Text
├── Image
├── SVG
├── Vector
├── Component
├── Instance
├── Boolean
└── Section
Scrollbar [Done]
```

Every node should have common properties.

### Common properties

```ts
type Node = {
  id: string
  type: NodeType

  name: string

  x: number
  y: number
  width: number
  height: number

  rotation: number
  opacity: number

  visible: boolean
  locked: boolean

  parentId?: string
  children?: string[]

  fills: Fill[]
  strokes: Stroke[]

  effects: Effect[]

  layout?: LayoutProperties
  constraints?: Constraints

  style?: StyleReference
}
```

But there's an important distinction:

**Don't make `x/y/width/height` the only source of truth.**

Your layout system needs to know **how those values were produced**.

---

# 3. Sizing system — this is where your Figma clone becomes powerful

I'd support these sizing modes:

### Width

```text
FIXED
HUG
FILL
MIN/MAX
```

For example:

```ts
width: {
  mode: "FIXED",
  value: 300
}
```

or:

```ts
width: {
  mode: "HUG"
}
```

or:

```ts
width: {
  mode: "FILL"
}
```

or:

```ts
width: {
  mode: "FILL",
  min: 200,
  max: 600
}
```

Same for height.

This gives you:

```text
Fixed width
     ↓
300px

Hug contents
     ↓
Text + padding

Fill parent
     ↓
available parent width

Min/max
     ↓
200px ←→ 600px
```

This should be implemented **before** you build advanced components.

---

# 4. Auto Layout / Flex Layout

Your Frame should have:

```ts
type LayoutProperties = {
  mode: "NONE" | "HORIZONTAL" | "VERTICAL"

  gap: number

  paddingTop: number
  paddingRight: number
  paddingBottom: number
  paddingLeft: number

  alignItems:
    | "START"
    | "CENTER"
    | "END"
    | "STRETCH"

  justifyContent:
    | "START"
    | "CENTER"
    | "END"
    | "SPACE_BETWEEN"

  wrap: boolean

  widthMode: "FIXED" | "HUG" | "FILL"
  heightMode: "FIXED" | "HUG" | "FILL"
}
```

Example:

```text
┌─────────────────────────────┐
│  padding                     │
│  ┌─────┐   ┌─────┐   ┌─────┐│
│  │ A   │   │ B   │   │ C   ││
│  └─────┘   └─────┘   └─────┘│
│       ← gap →                │
└─────────────────────────────┘
```

Then your editor can provide:

**Auto Layout**

- Horizontal
- Vertical
- Gap
- Padding
- Alignment
- Distribution
- Wrap
- Absolute positioning

---

# 5. Absolute positioning

You absolutely need this.

Inside an Auto Layout frame, a child should be able to say:

```ts
position: "ABSOLUTE"
```

Then:

```ts
x: 20
y: 30
```

instead of participating in the flex layout.

This enables things like:

```text
┌──────────────────────────┐
│                     🔔   │
│                          │
│       CONTENT            │
│                          │
│                 [BUTTON] │
└──────────────────────────┘
```

The notification icon can be absolute while the rest uses Auto Layout.

---

# 6. Constraints

For responsive resizing, build constraints separately from Auto Layout.

For example:

```ts
type Constraints = {
  horizontal:
    | "LEFT"
    | "RIGHT"
    | "LEFT_RIGHT"
    | "CENTER"
    | "SCALE"

  vertical:
    | "TOP"
    | "BOTTOM"
    | "TOP_BOTTOM"
    | "CENTER"
    | "SCALE"
}
```

Example:

```text
Parent
┌───────────────────────────────┐
│                               │
│          ┌─────────┐          │
│          │ Button  │          │
│          └─────────┘          │
│                               │
└───────────────────────────────┘
```

Constraint:

```text
horizontal = CENTER
vertical   = CENTER
```

Resize parent:

```text
Before                 After

┌────────────┐         ┌────────────────────┐
│            │         │                    │
│   Button   │   →     │       Button       │
│            │         │                    │
└────────────┘         └────────────────────┘
```

---

# 7. Component list

I'd build these in phases.

### Basic shapes

| Component | Important properties |
|---|---|
| Frame | layout, padding, sizing, constraints |
| Rectangle | radius, fill, stroke |
| Ellipse | fill, stroke |
| Line | stroke, endpoints |
| Polygon | sides, radius |
| Star | points, inner radius |
| Vector | paths |
| Boolean | union/intersect/subtract/exclude |

### Content

| Component | Important properties |
|---|---|
| Text | font, size, weight, line-height, letter-spacing |
| Image | fit, crop, position |
| Video | fit, controls |
| SVG | source, fill/stroke |

### Structural

| Component | Important properties |
|---|---|
| Group | children |
| Frame | children + layout |
| Section | children |
| Component | master definition |
| Instance | component reference |
| Slot | replaceable content |

---

# 8. Text is a major subsystem

Don't treat text as a simple rectangle.

You'll eventually need:

```ts
type TextProperties = {
  content: string

  fontFamily: string
  fontSize: number
  fontWeight: number

  lineHeight: number | "AUTO"
  letterSpacing: number

  textAlign: "LEFT" | "CENTER" | "RIGHT" | "JUSTIFY"

  textCase: "ORIGINAL" | "UPPER" | "LOWER"

  textDecoration:
    | "NONE"
    | "UNDERLINE"
    | "STRIKETHROUGH"

  sizing:
    | "FIXED"
    | "AUTO_WIDTH"
    | "AUTO_HEIGHT"

  maxLines?: number
}
```

And importantly:

### Text should affect parent sizing.

Example:

```text
Frame
 └── Text
       "Hello World"
```

Change:

```text
Hello World
```

to:

```text
Hello World, this is a much longer sentence.
```

The parent should potentially resize automatically.

That dependency is fundamental to a good layout engine.

---

# 9. Component system

You need two different concepts:

```text
Component Definition
        │
        ├── Component
        │
        └── Instances
              ├── Instance
              ├── Instance
              └── Instance
```

For example:

```text
Button Component

┌───────────────────┐
│      Continue     │
└───────────────────┘
```

Instances:

```text
Login screen
   ↓
Button Instance

Checkout screen
   ↓
Button Instance

Settings screen
   ↓
Button Instance
```

Changing the master component updates instances.

---

# 10. Variants

You'll want:

```text
Button
├── Variant: Primary
├── Variant: Secondary
├── Variant: Danger
└── Variant: Disabled
```

Model:

```ts
type VariantProperty = {
  name: string
  value: string
}
```

Instance:

```ts
variantProperties: {
  Type: "Primary",
  Size: "Large",
  State: "Hover"
}
```

---

# 11. Design variables

This is another major subsystem.

Support:

```text
Color
Number
String
Boolean
Font
```

Example:

```ts
variables = {
  colorPrimary: "#635BFF",
  spacingSmall: 8,
  spacingMedium: 16,
  spacingLarge: 24,
  radiusMedium: 8
}
```

Then a component doesn't store:

```text
padding = 16
```

It stores:

```text
padding = $spacingMedium
```

Change the variable → everything updates.

---

# 12. Styles

Create reusable:

### Paint styles

```text
Primary / 500
Gray / 900
Danger / 500
```

### Text styles

```text
Heading / H1
Heading / H2
Body / Large
Body / Medium
Caption
```

### Effect styles

```text
Shadow / Small
Shadow / Medium
Shadow / Large
```

### Grid styles

```text
Desktop Grid
Tablet Grid
Mobile Grid
```

---

# 13. Responsive breakpoints

For a truly responsive system, I'd add:

```ts
type Breakpoint = {
  id: string
  minWidth?: number
  maxWidth?: number

  overrides: {
    [nodeId: string]: Partial<Node>
  }
}
```

Example:

```text
Desktop
> 1024

Tablet
768–1023

Mobile
< 768
```

Then:

```text
Desktop
┌─────────────────────────────┐
│ Logo     Nav Nav Nav Button │
└─────────────────────────────┘

Mobile
┌───────────────────┐
│ Logo          ☰   │
└───────────────────┘
```

But **don't rely entirely on breakpoints**.

A good system should primarily use:

**Auto Layout + Fill + Hug + Min/Max + Constraints**

and use breakpoints for intentional design changes.

---

# 14. Your Inspector panel

Once the engine works, your right sidebar can expose:

```text
DESIGN

Position
X        120
Y         80

Size
W        320
H        200

Sizing
Width    [Fixed ▼]
Height   [Hug ▼]

Layout
Direction     [Horizontal]
Gap           16
Padding       16 16 16 16

Alignment
[←] [↔] [→]

Constraints
Horizontal   [Left ▼]
Vertical     [Top ▼]

Appearance
Opacity      100%
Radius       8

Fill
████████

Stroke
████████

Effects
Shadow
```

---

# 15. Layers panel

Your layer tree should correspond directly to your document tree.

```text
Page
│
├── Header
│   ├── Logo
│   ├── Navigation
│   └── Button
│
├── Hero
│   ├── Heading
│   ├── Description
│   └── CTA
│
└── Footer
```

Don't create a separate "layer model."

Your **document tree IS the layer tree**.

---

# 16. The canvas

Your canvas needs:

```text
Infinite Canvas
      ↓
Viewport
      ↓
World coordinates
      ↓
Nodes
```

Support:

- Pan
- Zoom
- Multi-select
- Box selection
- Resize handles
- Rotation
- Snap
- Guides
- Rulers
- Alignment
- Distribution
- Copy/paste
- Duplicate
- Group/ungroup
- Lock/hide
- Keyboard shortcuts

---

# 17. Snapping system

Build this separately.

Possible snap targets:

```text
Node edges
Node centers
Parent edges
Parent center
Grid
Guides
Other nodes
Spacing
```

Example:

```text
┌───────────────┐
│               │
│       │       │
│       │       │
└───────┼───────┘
        ↑
      center
```

---

# 18. Undo/redo

Don't implement undo by taking screenshots of the entire document.

Use commands/operations.

```ts
type Command = {
  execute(): void
  undo(): void
}
```

Examples:

```text
CreateNodeCommand
DeleteNodeCommand
MoveNodeCommand
ResizeNodeCommand
SetPropertyCommand
GroupNodesCommand
```

Then:

```text
Undo stack

[ Move ]
[ Resize ]
[ Change Fill ]
[ Create ]
```

This becomes extremely important later for collaboration.

---

# 19. Copy/paste format

Your internal document could look approximately like:

```json
{
  "version": 1,
  "pages": [
    {
      "id": "page_1",
      "name": "Home",
      "children": ["frame_1"]
    }
  ],
  "nodes": {
    "frame_1": {
      "type": "FRAME",
      "name": "Hero",
      "children": ["text_1", "button_1"],

      "layout": {
        "mode": "VERTICAL",
        "gap": 16,
        "padding": 32
      },

      "width": {
        "mode": "FILL"
      },

      "height": {
        "mode": "HUG"
      }
    }
  }
}
```

This gives you a very powerful foundation.

---

# 20. Most important: build the layout engine first

I'd implement the project in this order:

### Phase 1 — Document engine

```text
Node
Tree
Pages
Serialization
Selection
CRUD
```

### Phase 2 — Basic renderer

```text
Frame
Rectangle
Ellipse
Text
Image
```

### Phase 3 — Geometry

```text
x/y
width/height
rotation
scale
bounding boxes
transforms
```

### Phase 4 — Layout engine ⭐

```text
Fixed
Hug
Fill
Min/max
Padding
Gap
Horizontal
Vertical
Alignment
Absolute positioning
```

### Phase 5 — Constraints

```text
Left
Right
Center
Scale
Top
Bottom
```

### Phase 6 — Editor interactions

```text
Drag
Resize
Rotate
Snap
Guides
Multi-select
```

### Phase 7 — Components

```text
Component
Instance
Variants
Overrides
Slots
```

### Phase 8 — Design system

```text
Variables
Styles
Tokens
Libraries
```

### Phase 9 — Responsive system

```text
Breakpoints
Overrides
Responsive preview
Device presets
```

### Phase 10 — Collaboration

```text
Undo/redo
History
Multiplayer
Presence
Comments
Versioning
```

---

# 21. The critical part: don't build your own CSS engine blindly

For your layout engine, I'd seriously consider using **Yoga** as the underlying Flexbox calculation engine, or implementing a compatible layout abstraction around it.

Your architecture could be:

```text
Your Design Model
        ↓
Layout Adapter
        ↓
Yoga / Flexbox calculation
        ↓
Computed Layout
        ↓
Renderer
```

The important distinction is:

```text
DESIGN VALUES
     ↓
Layout Engine
     ↓
COMPUTED VALUES
```

For example:

```text
Design:

width = FILL
padding = 24
gap = 16


Computed at 1440px:

width = 1392px


Resize canvas to 800px:

width = 752px


Resize to 390px:

width = 342px
```

The designer shouldn't be storing all those computed values as permanent properties.

---

# 22. Your first MVP

Don't try to clone all of Figma initially.

Build this:

```text
             FIGMA-LIKE MVP

┌──────────────────────────────────────────┐
│ Toolbar                                  │
├────────────┬─────────────────┬───────────┤
│            │                 │           │
│  Layers    │     Canvas      │ Inspector │
│            │                 │           │
│  Frame     │                 │ Position  │
│   ├ Text   │                 │ Size      │
│   └ Button │                 │ Layout    │
│            │                 │ Fill      │
│            │                 │ Stroke    │
└────────────┴─────────────────┴───────────┘
```

Only support:

```text
Frame
Rectangle
Text
Image

+ Select
+ Move
+ Resize

+ Horizontal Auto Layout
+ Vertical Auto Layout

+ Fixed
+ Hug
+ Fill

+ Padding
+ Gap

+ Basic constraints
```

If **this** works beautifully, you have the foundation for the rest.

---


1.  Page
2.  Section
3.  Frame [Done]
4.  Group [Done]

5.  Rectangle [Done]
6.  Rounded Rectangle [Done]
7.  Ellipse / Circle [Done]
8.  Line [Done]
9.  Arrow [Done]
10. Polygon [Done]
11. Star [Done]

12. Vector [Done]
13. SVG [Done]
14. Boolean Group
    - Union
    - Subtract
    - Intersect
    - Exclude

15. Text
16. Rich Text
17. Image
18. Video
19. GIF

20. Component
21. Component Set
22. Instance
23. Slot

24. Embed
25. Link
