# Component design

A component is one visual object with one name. That name is PascalCase, and the same name is used for the XML definition and the C++ class.

```text
components/Button/Button.xml
components/Button/Button.h
components/Button/Button.cpp
```

`Button.xml` is the source of truth for structure, layout, and simple interaction. `Button.h` and `Button.cpp` are the typed object the runtime measures, arranges, paints, and connects to events. Application logic stays outside the component and is called by those events.

Build new components on the existing primitives: `frame`, `text`, `image`, `vector`, `path`, `line`, `group`, `component`, `instance`, `slot`, and `scroll`. Do not add a new primitive when a `frame` plus properties can express the object.

## XML

Element names in a document are lowercase. The component file name is the PascalCase type name.

```xml
<component name="Button">
    <property name="label" type="string"/>
    <property name="enabled" type="bool"/>

    <frame id="face" layout="horizontal" align="center" justify="center"
           padding="16" radius="8" cursor="pointer">
        <slot name="icon"/>
        <text value="{label}"/>
    </frame>
</component>
```

An instance fills the properties and the slots:

```xml
<instance component="Button" label="Save" enabled="true" onClick="save">
    <slot name="icon">
        <image source="@assets/save.png" width="16" height="16"/>
    </slot>
</instance>
```

### Document shape

A component file has one root. Attributes are `name="value"` with either quote. Comments are `<!-- ... -->`. These character references are decoded: `&lt;`, `&gt;`, `&amp;`, `&quot;`, `&apos;`, and numeric references.

```text
document  = element
element   = '<' name attribute* '/>'
          | '<' name attribute* '>' element* '</' name '>'
attribute = name '=' string
```

Ids are unique in the document. The tree is at most 64 levels deep. An unknown element or property is an error. A property is written once.

### Values

| Type | Written as | Example |
| --- | --- | --- |
| string | text | `label="Save"` |
| bool | `true` or `false` | `enabled="false"` |
| int | integer | `maxLines="2"` |
| float | number | `opacity="0.5"` |
| color | `#RGB`, `#RGBA`, `#RRGGBB`, `#RRGGBBAA`, or a named color | `background="#2563EB"` |
| length | number, `px`, `%`, `fill`, `fit` | `width="120"` |
| edges | one to four lengths | `padding="8 16"` |
| alignment | `start`, `center`, `end`, `stretch`, `left`, `right`, `top`, `bottom` | `align="center"` |
| event | C++ identifier | `onClick="save"` |

A bare number on a length is pixels. Named colors are `black`, `white`, `red`, `green`, `blue`, `gray`, `grey`, and `transparent`. There is no conversion from an arbitrary string into a length or a color.

`{label}` is a one-way binding to a component property. `{user.name}` is a one-way binding to application data. `{value}` on an input is two-way. The braces are the whole attribute value, not mixed into other text.

An event value is a letter or `_`, then letters, digits, or `_`. It names a C++ function. It is not a string of script.

### What belongs in XML

Structure, layout, visual properties, slots, and calls that only set a property, show or hide a node, or start an animation. Anything that reads files, talks to a service, or decides application state is a named C++ handler.

Paint with solid fills, 1px borders, corner radius, clips, and text. Do not paint shadows or gradients.

## C++

`Button.h` declares the class. `Button.cpp` defines it. Both live in `namespace Runtime`.

```cpp
#pragma once

#include "ui/element/Element.h"

#include <string>

namespace Runtime {

class Button : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    void OnPaint(Renderer& renderer) override;

    void SetLabel(std::string value);
    const std::string& Label() const;

private:
    std::string label;
};

}  // namespace Runtime
```

The class inherits `Element`. `Kind` returns the catalog kind. `Measure` reports the preferred size in DIPs. `Arrange` places this node and its children. `OnPaint` draws only this node's own pixels; children are painted by the element base. Hit testing, hover, press, and key input are overrides on `Element` when the component is interactive.

A property in XML becomes a `Set` function and a getter. `label` is `SetLabel` and `Label`. `onClick` is a `std::function<void()>` set by the application, not a string stored for later lookup.

### Naming

| Thing | Form | Example |
| --- | --- | --- |
| File, class, struct, function | PascalCase | `Button`, `SetLabel`, `Measure` |
| Variable, parameter, member | camelCase | `label`, `availableWidth` |
| Constant | CAPITAL_CASE | `DEFAULT_RADIUS` |
| Enumerator | CAPITAL_CASE | `NodeKind::FRAME` |
| XML element | lowercase | `frame`, `text` |
| XML attribute | camelCase | `fontSize`, `onClick` |

Members do not use a trailing underscore. Do not name an enumerator `ERROR`. Diagnostic severity uses `FAILURE`.

Types and functions that the compiler and the factory call are public. Layout scratch and the property storage are private.

### Language

C++20. The component includes project headers only. It does not include Direct3D, Direct2D, DirectWrite, or Windows Imaging headers. Drawing goes through `Renderer`. Text measurement goes through `TextMeasurer`. Colors, lengths, and rectangles come from `engine/math` and `engine/types`.

No third-party library. No global mutable state. A component does not open files, create windows, or start threads. Image bytes are already decoded before paint. Missing images stay a placeholder painted by the image element.

Layout units are DIPs. Do not convert to physical pixels inside the component.

### Control flow

`Measure` does not paint and does not change children except to measure them. `Arrange` writes bounds and does not allocate the child list again. `OnPaint` reads the arranged bounds and the current properties. A property change invalidates that component, not the whole tree.

Events are member functions. The XML name `onClick="save"` is connected by the application as a typed call, in the same way as `button.SetOnClick(&App::Save)`. Do not dispatch with a string name at runtime.

## Adding a component

1. Create `components/Name/Name.xml`, `Name.h`, and `Name.cpp`.
2. Describe the visual tree and the public properties in the XML file.
3. Declare the class, the property setters, and the `Element` overrides in the header.
4. Implement measure, arrange, and paint in the cpp file.
5. Register the element name, its properties, and their types in the component catalog so the compiler accepts the XML.
6. Teach `Instantiate` to construct the class from the compiled node.
7. Add a document that uses the component and a test that compiles it and checks one laid-out result.

Copy `Frame` when the new object is mostly a container. Copy a leaf such as `text` when it has no children. Slots are the extension point for content the instance supplies. Variants are properties, not extra classes.
