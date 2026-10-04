The filenames must be PascalCase. The functions, classes, and structs must be PascalCase. Variable names must be camelCase. Constants must be CAPITAL_CASE.

The key is: **keep the number of primitives small, but make their properties, layout, states, animation, events, and composition capabilities extremely powerful.**

# 1. Core primitives

I recommend these **11**:

```text
Frame ✅
Text ✅
Image ✅
Vector ✅
Path ✅
Line ✅
Group ✅
Component ✅
Instance ✅
Slot ✅
Scroll ✅
```

You can build virtually everything else on top of these.

---

# 2. `Frame`

This is your **most important primitive**.

Think of it as Figma's Frame + a general-purpose visual container.

```xml
<frame>
    ...
</frame>
```

### Identity

```text
id ✅
name ✅
class ✅
component ✅
instance ✅
tag ✅
role ✅
```

### Size

```text
width ✅
height ✅

minWidth ✅
maxWidth ✅
minHeight ✅
maxHeight ✅

widthMode:
    fixed ✅
    fill ✅
    fit ✅
    percent ✅

heightMode:
    fixed ✅
    fill ✅
    fit ✅
    percent ✅

aspectRatio ✅
```

### Position

```text
x ✅
y ✅
zIndex ✅

position:
    flow ✅
    absolute ✅
    fixed ✅
    sticky

anchor:
    top ✅
    right ✅
    bottom ✅
    left ✅
    center ✅
```

### Padding

```text
padding ✅
paddingTop ✅
paddingRight ✅
paddingBottom ✅
paddingLeft ✅
```

### Margin

```text
margin ✅
marginTop ✅
marginRight ✅
marginBottom ✅
marginLeft ✅
```

### Layout

```text
layout:
    none ✅
    horizontal ✅
    vertical ✅
    grid ✅
    wrap ✅
    stack ✅
```

### Flex-like properties

```text
grow ✅
shrink
basis ✅

gap ✅
rowGap ✅
columnGap ✅

align ✅
justify ✅

wrap ✅
```

### Alignment

```text
left ✅
center ✅
right ✅

top ✅
center ✅
bottom ✅

start ✅
end ✅
stretch ✅
```

### Grid

```text
columns ✅
rows

columnGap ✅
rowGap ✅

column
row

columnSpan ✅
rowSpan
```

### Background

```text
background ✅
backgroundColor ✅
gradient
image
opacity ✅
```

Support:

```text
solid ✅
linearGradient
radialGradient
conicGradient
image
```

### Border

```text
borderWidth ✅
borderColor ✅
borderStyle ✅

borderTop ✅
borderRight ✅
borderBottom ✅
borderLeft ✅
```

### Corner radius

```text
radius ✅
radiusTopLeft ✅
radiusTopRight ✅
radiusBottomRight ✅
radiusBottomLeft ✅
```

### Shadow

```text
shadow
shadowColor
shadowX
shadowY
shadowBlur
shadowSpread
```

Allow multiple shadows:

```xml
<shadow .../>
<shadow .../>
```

### Clipping

```text
overflow:
    visible ✅
    hidden ✅
    scroll ✅
    auto ✅

clip ✅
clipRadius
clipPath
```

### Transform

```text
translateX ✅
translateY ✅

scale
scaleX
scaleY

rotation
rotationX
rotationY
rotationZ

skewX
skewY

transformOrigin
```

### Opacity / visibility

```text
opacity ✅

visible ✅
hidden ✅

display:
    normal ✅
    none ✅
```

### Blending

```text
blendMode
```

Examples:

```text
normal
multiply
screen
overlay
darken
lighten
```

### Effects

```text
blur
backdropBlur
brightness
contrast
saturation
grayscale
hueRotate
```

### Cursor

```text
cursor:
    default ✅
    pointer ✅
    text
    grab
    grabbing
    resize
    crosshair
    notAllowed
```

---

# 3. `Text`

Your complete text primitive should support:

```text
text ✅
```

### Typography

```text
fontFamily
fontSize ✅
fontWeight ✅
fontStyle

letterSpacing
wordSpacing
lineHeight

textColor ✅
```

### Alignment

```text
textAlign:
    left ✅
    center ✅
    right ✅
    justify

verticalAlign:
    top
    center
    bottom
```

### Wrapping

```text
wrap
maxLines
ellipsis
overflow
```

### Decoration

```text
underline
lineThrough
overline

decorationColor
decorationWidth
decorationStyle
```

### Text transform

```text
none
uppercase
lowercase
capitalize
```

### Advanced

```text
fontFeatures
language
direction
writingMode
```

Support:

- Unicode
- emoji
- RTL
- Arabic
- complex scripts
- fallback fonts
- text shaping

---

# 4. `Image`

```xml
<image source="@assets/photo.png"/>
```

Properties:

```text
source ✅
width ✅
height ✅

fit:
    fill
    contain
    cover
    none
    scaleDown

position
opacity ✅

radius ✅
clip

tint
brightness
contrast
saturation

flipX
flipY
rotation
```

Support:

```text
PNG
JPEG
WebP
AVIF
SVG
animated images
```

---

# 5. `Vector`

This is your arbitrary vector shape primitive.

```xml
<vector>
    ...
</vector>
```

Properties:

```text
fill ✅
stroke ✅
strokeWidth ✅
strokeOpacity

lineCap
lineJoin

opacity

fillRule

transform
```

Support paths:

```text
M
L
H
V
C
S
Q
T
A
Z
```

This allows arbitrary icons, logos and shapes.

---

# 6. `Path`

You can optionally make `Path` a lower-level primitive than `Vector`.

```xml
<path
    d="M10 10 L100 10 L50 80 Z"/>
```

Properties:

```text
d ✅
fill
stroke ✅
strokeWidth ✅
lineCap
lineJoin
dash
dashOffset
```

---

# 7. `Line`

```xml
<line
    x1="0"
    y1="0"
    x2="100"
    y2="0"/>
```

Properties:

```text
stroke ✅
strokeWidth ✅
opacity ✅
lineCap
dash
dashGap
```

---

# 8. `Group`

A group simply organizes objects.

```xml
<group>
    <text/>
    <image/>
    <vector/>
</group>
```

Properties:

```text
id ✅
name ✅
opacity ✅
transform
rotation
scale
visibility
```

A group should **not impose layout**.

That's the difference between:

```text
Group → organization
Frame → layout/container
```

---

# 9. `Component`

This is the reusable definition.

```xml
<component name="Button">
    ...
</component>
```

Supports:

```text
properties
events
slots
states
animations
styles
variants
bindings
```

Example:

```xml
<component name="Button">

    <property name="label" type="string"/>
    <property name="variant" type="ButtonVariant"/>

    <frame>
        <slot name="icon"/>
        <text value="{label}"/>
    </frame>

</component>
```

---

# 10. `Instance`

Instance of a component:

```xml
<instance component="Button"/>
```

Allow overrides:

```text
properties
styles
content
slots
states
```

Example:

```xml
<instance component="Button"
          label="Save"
          variant="primary"/>
```

This is extremely important for your App Designer.

---

# 11. `Slot`

Allows arbitrary content injection.

```xml
<component name="Card">

    <frame>
        <slot name="header"/>
        <slot name="content"/>
        <slot name="footer"/>
    </frame>

</component>
```

Then:

```xml
<Card>

    <slot name="header">
        <Text value="Profile"/>
    </slot>

    <slot name="content">
        ...
    </slot>

</Card>
```

This gives you enormous composability.

---

# 12. `Scroll`

You can either make scrolling a Frame property or have a dedicated primitive.

I'd support:

```text
scrollX
scrollY
scrollBoth
```

Properties:

```text
direction
offset
contentWidth
contentHeight

scrollbar
scrollbarVisibility

overscroll
scrollPhysics

smoothScrolling
```

---

# 13. Layout/constraints

This is arguably more important than the primitives themselves.

Support:

### Auto layout

```text
direction
gap
padding
alignment
distribution
wrap
```

### Constraints

```text
left
right
top
bottom
centerX
centerY

minWidth
maxWidth
minHeight
maxHeight

aspectRatio
```

### Anchoring

```xml
<frame
    left="parent.left"
    right="parent.right"
    top="parent.top"/>
```

### Responsive

```text
breakpoints
container queries
orientation
screen size
aspect ratio
```

---

# 14. Styles

Don't put every visual property directly on every element.

Create a style system.

```xml
<style name="button">
    <background color="$primary"/>
    <radius value="8"/>
    <padding horizontal="16" vertical="10"/>
</style>
```

Then:

```xml
<frame style="button"/>
```

Styles should support:

```text
inheritance
variables
tokens
states
variants
responsive overrides
animations
```

---

# 15. Design tokens

Your framework should have:

```text
colors
spacing
typography
radii
shadows
sizes
durations
easing
breakpoints
```

Example:

```xml
<tokens>

    <color name="primary" value="#2563EB"/>
    <color name="surface" value="#FFFFFF"/>

    <spacing name="sm" value="8"/>
    <spacing name="md" value="16"/>

    <radius name="md" value="8"/>

</tokens>
```

Then:

```xml
background="$primary"
padding="$spacing.md"
radius="$radius.md"
```

---

# 16. States

Every interactive component should support standardized states:

```text
normal
hover
pressed
focused
focusVisible
disabled
selected
checked
expanded
collapsed
loading
error
success
dragging
dragOver
readonly
```

Example:

```xml
<style name="button">

    <state name="normal">
        ...
    </state>

    <state name="hover">
        ...
    </state>

    <state name="pressed">
        ...
    </state>

    <state name="disabled">
        ...
    </state>

</style>
```

---

# 17. Animation system

For animations, **don't create an animation system for every component**.

Create one universal animation engine that can animate any animatable property.

For example:

```xml
<animate
    target="button"
    property="scale"
    from="1"
    to="1.05"
    duration="150ms"
    easing="easeOut"/>
```

## Properties that should be animatable

```text
x
y
width
height

opacity

scale
scaleX
scaleY

rotation
rotationX
rotationY

backgroundColor
color
borderColor

radius

shadow
blur

transform
```

Eventually:

```text
path
gradient
clip
filter
```

---

# 18. Animation types

Implement:

### Transition

```text
hover:
    scale 1 → 1.05
```

### Keyframes

```xml
<animation name="pulse">

    <keyframe time="0%" opacity="1"/>
    <keyframe time="50%" opacity="0.5"/>
    <keyframe time="100%" opacity="1"/>

</animation>
```

### Sequence

```text
A → B → C
```

### Parallel

```text
opacity + scale + rotation
```

### Stagger

For lists:

```text
item 1 → 0ms
item 2 → 50ms
item 3 → 100ms
...
```

### Spring

```text
mass
stiffness
damping
velocity
```

### Easing

```text
linear
easeIn
easeOut
easeInOut

cubicBezier

spring
```

---

# 19. Animation triggers

Animations should be triggered by:

```text
onMount
onUnmount
onHover
onPress
onFocus
onStateChange
onPropertyChange
onScroll
onDrag
onLoad
onComplete
```

Example:

```xml
<animation
    trigger="hover"
    property="scale"
    from="1"
    to="1.05"
    duration="150ms"/>
```

---

# 20. Complete event system

I would organize events into categories.

### Pointer/mouse

```text
pointerDown
pointerUp
pointerMove
pointerEnter
pointerLeave
pointerOver
pointerOut
pointerCancel
click
doubleClick
contextMenu
```

Use pointer events as the fundamental system rather than creating completely separate mouse/touch systems.

---

### Touch

```text
touchStart
touchMove
touchEnd
touchCancel
```

### Keyboard

```text
keyDown
keyUp
keyPress
```

Properties/event data:

```text
key
code
modifiers
ctrl
shift
alt
meta
```

### Focus

```text
focus
blur
focusIn
focusOut
```

### Input

```text
input
change
beforeInput
compositionStart
compositionUpdate
compositionEnd
```

### Form

```text
submit
reset
validate
invalid
```

### Drag & Drop

```text
dragStart
drag
dragEnter
dragOver
dragLeave
drop
dragEnd
```

### Scroll

```text
scroll
scrollStart
scrollEnd
```

### Lifecycle

```text
create
mount
unmount
destroy
load
unload
```

### Layout

```text
resize
layout
positionChange
sizeChange
```

### Animation

```text
animationStart
animationUpdate
animationComplete
animationCancel
```

### Component/state

```text
stateChange
propertyChange
variantChange
```

### Selection

```text
select
deselect
selectionChange
```

### Media

```text
loadStart
load
loadError
play
pause
ended
timeUpdate
```

### Accessibility

Your runtime should internally handle:

```text
focus navigation
screen reader actions
keyboard activation
accessibility state changes
```

---

# 21. Events should be strongly typed

Don't make events strings everywhere.

Instead:

```xml
<Button onClick="save"/>
```

should compile to something conceptually like:

```cpp
button.onClick.connect(&App::save);
```

rather than:

```cpp
button.on("click", "save");
```

That keeps your performance goal.

---

# 22. Actions

Events tell you **what happened**.

Actions tell you **what to do**.

Your XML can support built-in actions:

```text
navigate
back
forward

open
close
toggle

show
hide

focus
blur

set
reset

animate

scrollTo

select
deselect

submit

copy
paste

download

openURL

showDialog
showToast

play
pause
stop
```

Example:

```xml
<Button
    onClick="
        set(user.loggedIn, true);
        navigate('/dashboard');
    "/>
```

For complex logic:

```xml
<Button onClick="login"/>
```

and `login()` remains C++.

That gives you the ideal division:

```text
XML → UI + simple interaction
C++ → application/business logic
```

---

# 23. Data binding

You also need this for functionality.

```xml
<Text value="{user.name}"/>

<Input value="{user.email}"/>

<Frame visible="{user.isLoggedIn}"/>
```

Support:

```text
oneWay
twoWay
oneTime
computed
reactive
async
```

And changes should be incremental:

```text
user.name changes
       ↓
dependency graph
       ↓
Text invalidated
       ↓
Text updated
```

Not:

```text
user.name changes
       ↓
rebuild entire UI
```

---

# 24. What your final primitive model looks like

I'd aim for:

```text
                 CORE UI
                    │
       ┌────────────┼────────────┐
       │            │            │
    Visual        Layout       Logic
       │            │            │
       ▼            ▼            ▼
    Frame        Flex/Grid     Events
    Text         Constraints   Actions
    Image        Alignment     Binding
    Vector       Responsive    State
    Path
    Line
    Group
       │
       ▼
   Composition
       │
 Component
 Instance
 Slot
```