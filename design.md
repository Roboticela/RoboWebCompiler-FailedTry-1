# Component design

A component is one visual object with one PascalCase name. That name is the XML file, the header, and the source file.

```text
components/Button/Button.xml
components/Button/Button.h
components/Button/Button.cpp
```

`Button.xml` is the source of truth for structure, layout, and simple interaction. `Button.h` and `Button.cpp` are the typed object the runtime measures, arranges, paints, and connects to events. Application logic stays outside the component and is called by those events.

Open a component folder with:

```bat
OpenComponent.bat components\Button
```

The folder name must match the three files. **Run** in the designer saves the open XML and starts it.

Build screens from the elements below. Do not add a new primitive when `frame` plus properties can express the object.

## XML syntax

A document has one root element. Attributes are `name="value"` or `name='value'`. Comments are `<!-- ... -->` and are not kept when the file is saved. An XML declaration is skipped.

```text
document  = element
element   = '<' name attribute* '/>'
          | '<' name attribute* '>' element* '</' name '>'
attribute = name '=' string
```

Decoded character references: `&lt;` `&gt;` `&amp;` `&quot;` `&apos;` and numeric references such as `&#65;` and `&#x41;`.

Element names are lowercase. Attribute names are camelCase. An unknown element or property is an error. Each property is written once. Ids are unique. The tree is at most 64 levels deep. `x` and `y` may be negative. Other pixel lengths may not.

A document that runs as an app uses `window` as the root:

```xml
<window width="800" height="600" title="Runtime" background="#FFFFFF">
    <frame id="root" layout="vertical" width="fill" height="fill" padding="32" gap="16">
        <text value="Hello" fontSize="17" textColor="#1D1D1F"/>
    </frame>
</window>
```

`window` `width` and `height` are pixel lengths. They size the client area in DIPs.

### Values

| Type | Syntax | Example |
| --- | --- | --- |
| string | text | `title="Runtime"` |
| bool | `true` or `false` | `visible="false"` |
| int | optional sign and digits | `maxLines="2"` `zIndex="-1"` |
| float | optional sign, digits, optional fraction | `opacity="0.5"` `scale="1.05"` |
| font size | pixel length | `fontSize="17"` `fontSize="17px"` `fontSize="$type.body"` |
| color | named color, hex, or a color token | `textColor="#1D1D1F"` |
| length | number, `px`, `%`, `fill`, `fit`, or a spacing or radius token | `width="120"` `width="50%"` `width="fill"` |
| edges | 1, 2, 3, or 4 lengths separated by spaces | `padding="8"` `padding="8 16"` `padding="8 16 8 16"` |
| alignment | one of the words below | `align="center"` |
| event | C++ identifier | `onClick="save"` |

There is no conversion from an arbitrary word into a length, color, bool, or number.

**Color names:** `black` `white` `red` `green` `blue` `gray` `grey` `transparent`.

**Hex:** `#RGB` `#RGBA` `#RRGGBB` `#RRGGBBAA`.

**Length.** A bare number is pixels. `120px` is pixels. `50%` is a percent of the parent. `fill` takes the remaining space. `fit` sizes to the content. One edge value applies to all four sides. Two values are vertical then horizontal. Three values are top, horizontal, bottom. Four values are top, right, bottom, left.

**Alignment** (`align`): `start` `left` `top` `center` `end` `right` `bottom` `stretch`. `left` and `top` are `start`. `right` and `bottom` are `end`.

**zIndex** may be an integer or one of: `desktop` (0), `window` (10), `dock` (600), `menubar` (700), `notifications` (800), `spotlight` (900), `modal` (900), `contextMenu` (950), `tooltip` (1000).

### Tokens

A token is a `$` name. It is replaced before the value is parsed, so it can be used anywhere the replacement type is legal.

Foundations are built in. Colors, typography, spacing, radius, and layout are available in every document without declaring them. A document can replace a foundation, add its own token, or remove a foundation. The designer lists the same set in Variables.

```xml
<frame background="$bg-primary" padding="$space-4" radius="$radius-lg" width="$width-docs" gap="$space-3">
    <text text="Title" fontSize="$type-h1" fontWeight="$weight-semibold" letterSpacing="$tracking-h1" lineHeight="$leading-h1" textColor="$text-primary"/>
</frame>
```

```xml
<tokens>
    <color name="brand-primary" value="#112233"/>
    <color name="ink" value="#111111"/>
    <spacing name="space-4" removed="true"/>
</tokens>
```

The element name is the group: `color`, `type`, `spacing`, `radius`, or `layout`. The short name is what you write after `$`. `$color.brand-primary` is the same value as `$brand-primary`. A later token with the same name wins. `removed="true"` drops a built-in default.

| Group | Names |
| --- | --- |
| Colors | `brand-primary` `brand-primary-hover` `brand-primary-active` `brand-primary-disabled` `brand-primary-subtle` `accent-teal` `accent-teal-subtle` `success` `success-subtle` `warning` `warning-subtle` `danger` `danger-hover` `danger-subtle` `text-primary` `text-secondary` `text-tertiary` `text-disabled` `text-on-brand` `text-link` `bg-primary` `bg-secondary` `bg-tertiary` `bg-elevated` `bg-hover` `bg-active` `border-default` `border-subtle` `border-strong` `border-focus` `border-danger` |
| Typography | `type-display` `type-h1` `type-h2` `type-h3` `type-subhead` `type-content` `type-content-sm` `type-caption` `type-eyebrow` `type-label`, each with a `-mobile` size where the phone size differs, plus `weight-regular` `weight-medium` `weight-semibold`, `tracking-display` `tracking-h1` `tracking-h2` `tracking-h3` `tracking-eyebrow`, and `leading-display` `leading-h1` `leading-h2` `leading-h3` `leading-subhead` `leading-content` `leading-content-sm` `leading-caption` `leading-eyebrow` `leading-label` |
| Spacing | `space-1` 4 through `space-10` 128: 4, 8, 12, 16, 24, 32, 48, 64, 96, 128 |
| Radius | `radius-sm` 6, `radius-md` 8, `radius-lg` 12, `radius-xl` 16, `radius-2xl` 20, `radius-panel` 18, `radius-pill` 980 |
| Layout | `breakpoint-mobile` 734, `breakpoint-tablet` 1068, `breakpoint-desktop` 1069, `pad-mobile` 22, `pad-tablet` 40, `pad-desktop` 80, `gutter-mobile` 16, `gutter` 24, `columns-mobile` 1, `columns` 12, `width-copy` 692, `width-docs` 760, `width-wide` 900, `width-marketing` 980, `width-section` 1024 |

These older names still resolve: `$color.primary`, `$space.md`, `$radius.md`, `$type.h1`, `$weight.semibold`, `$color.success`, `$color.danger`, `$space.0`, `$radius.none`, `$type.body`, `$size.controlMedium`. A document token with the same key replaces the built-in value.

| Token | Value |
| --- | --- |
| `$color.primary` `$color.brand` `$color.focus` `$color.info` | `#0E7AFF` |
| `$color.primaryHover` `$color.brandHover` | `#0A6AE8` |
| `$color.primaryActive` `$color.brandActive` | `#0757C7` |
| `$color.onPrimary` `$color.onBrand` | `#FFFFFF` |
| `$color.secondary` `$color.muted` `$color.textSecondary` | `#6E6E73` |
| `$color.foreground` `$color.text` | `#1D1D1F` |
| `$color.textTertiary` | `#86868B` |
| `$color.background` | `#FFFFFF` |
| `$color.surface` | `#F5F5F7` |
| `$color.surfaceVariant` | `#EDEDEF` |
| `$color.border` | `#D2D2D7` |
| `$color.borderStrong` | `#B8B8BD` |
| `$color.success` | `#1D7A34` |
| `$color.warning` | `#9A6700` |
| `$color.error` `$color.danger` | `#FF453A` |
| `$color.overlay` | `#00000080` |
| `$space.0` | `0` |
| `$space.xs` `$space.1` | `4` |
| `$space.sm` `$space.2` | `8` |
| `$space.md` `$space.3` | `12` |
| `$space.lg` `$space.4` | `16` |
| `$space.xl` `$space.5` | `24` |
| `$space.2xl` `$space.6` | `32` |
| `$space.3xl` `$space.7` | `48` |
| `$radius.none` | `0` |
| `$radius.sm` | `6` |
| `$radius.md` | `8` |
| `$radius.lg` | `12` |
| `$radius.xl` | `16` |
| `$radius.pill` `$radius.circle` | `980` |
| `$type.display` | `80` |
| `$type.h1` `$type.heading1` | `56` |
| `$type.h2` `$type.heading2` | `40` |
| `$type.h3` `$type.heading3` | `28` |
| `$type.title` `$type.subhead` | `24` |
| `$type.body` | `17` |
| `$type.bodySmall` `$type.label` | `13` |
| `$type.caption` `$type.code` | `12` |
| `$weight.regular` | `400` |
| `$weight.medium` | `500` |
| `$weight.semibold` | `600` |
| `$size.controlSmall` | `32` |
| `$size.controlMedium` | `44` |
| `$size.controlLarge` | `50` |

```xml
<frame padding="$space.lg" radius="$radius.md" background="$color.surface">
    <text value="Title" fontSize="$type.h3" fontWeight="$weight.semibold" textColor="$color.foreground"/>
</frame>
```

### Bindings

The braces are the whole attribute value. Bindings are accepted on string and bool properties.

| Form | Mode | Meaning |
| --- | --- | --- |
| `{user.name}` | one way | Read `user.name`. |
| `{=email}` | two way | Read and write `email`. |
| `{!title}` | one time | Read once. |
| `{first + last}` | computed | A `+` inside the braces. |
| `{reactive status}` | reactive | Update when `status` changes. |
| `{async profile}` | async | Value arrives later. |

```xml
<text value="{user.name}"/>
<frame visible="{user.isLoggedIn}"/>
```

### Events and actions

An event attribute names a C++ function: a letter or `_`, then letters, digits, or `_`. It is not a script.

```xml
<frame onClick="save"/>
```

An action is a built-in call. Several actions are separated by `;`.

```xml
<frame onClick="set:user.loggedIn=true; navigate(/dashboard)"/>
```

Verbs, with or without parentheses: `navigate` `back` `forward` `open` `close` `toggle` `show` `hide` `focus` `blur` `set` `reset` `animate` `scrollTo` `select` `deselect` `submit` `copy` `paste` `download` `openURL` `showDialog` `showToast` `play` `pause` `stop`.

Shorthand: `show:name` `hide:name` `toggle:name` `submit:name` `set:path=value`.

A handler that is only an identifier stays a C++ call. Use an action when the work is one of the verbs above.

### Path data

`d` on `path` is a string of commands: `M` `L` `H` `V` `C` `S` `Q` `T` `A` `Z`.

```xml
<path d="M10 10 L100 10 L50 80 Z" fill="#0E7AFF"/>
```

### Images

`source` is a path. `@assets/photo.png` is an asset path. Decoded formats are PNG, JPEG, GIF (including animation), WebP, and SVG. A missing file paints a gray placeholder.

## Elements

`window` is the app root. The others may be nested where children are allowed. `text`, `image`, `vector`, `path`, `line`, and `shadow` take no children.

### Shared identity

`text`, `image`, `vector`, `path`, `line`, `group`, `component`, `instance`, and `slot` all accept:

| Property | Type | Values |
| --- | --- | --- |
| `id` `name` `style` | string | free text |
| `x` `y` `width` `height` | length | see Values |
| `position` | string | `flow` `absolute` `fixed` `sticky` |
| `opacity` | float | number |
| `visible` | bool | `true` `false` |

They also accept every event in the event list.

### `window`

Children: yes.

| Property | Type |
| --- | --- |
| `id` `title` | string |
| `width` `height` | length, pixels |
| `background` | color |

### `frame`

Children: yes. This is the container. Use it for layout, fill, border, radius, and clip.

| Property | Type | Values |
| --- | --- | --- |
| `id` `name` `class` `component` `instance` `tag` `role` | string | free text |
| `width` `height` `minWidth` `maxWidth` `minHeight` `maxHeight` `x` `y` | length | see Values |
| `widthMode` `heightMode` | string | `fixed` `fill` `fit` `percent` |
| `aspectRatio` `grow` `shrink` `opacity` | float | number |
| `zIndex` | int | integer or a z name |
| `position` | string | `flow` `absolute` `fixed` `sticky` |
| `anchor` | string | `top` `right` `bottom` `left` `center` |
| `padding` `margin` | edges | 1 to 4 lengths |
| `paddingTop` `paddingRight` `paddingBottom` `paddingLeft` | length | see Values |
| `marginTop` `marginRight` `marginBottom` `marginLeft` | length | see Values |
| `layout` | string | `none` `horizontal` `vertical` `grid` `wrap` `stack` |
| `basis` `gap` `rowGap` `columnGap` | length | see Values |
| `align` | alignment | see Values |
| `justify` | string | `start` `center` `end` `left` `right` `top` `bottom` `stretch` `spaceBetween` |
| `wrap` | string | `nowrap` `wrap` |
| `columns` `rows` `column` `row` `columnSpan` `rowSpan` | int | `columns` is at least 1 |
| `background` `backgroundColor` | color | see Values |
| `gradient` `image` `borderStyle` | string | `gradient` and `image` are free text. `borderStyle`: `solid` `dashed` `dotted` `none` |
| `borderWidth` `borderTop` `borderRight` `borderBottom` `borderLeft` `radius` | length | see Values |
| `borderColor` | color | see Values |
| `radiusTopLeft` `radiusTopRight` `radiusBottomRight` `radiusBottomLeft` | length | see Values |
| `shadow` `clipPath` `transformOrigin` `style` | string | free text |
| `shadowColor` | color | see Values |
| `shadowX` `shadowY` `shadowBlur` `shadowSpread` | float | number |
| `overflow` | string | `visible` `hidden` `scroll` `auto` |
| `clip` `visible` | bool | `true` `false` |
| `clipRadius` | length | see Values |
| `translateX` `translateY` `scale` `scaleX` `scaleY` | float | number |
| `rotation` `rotationX` `rotationY` `rotationZ` `skewX` `skewY` | float | number |
| `visibility` | string | `visible` `hidden` |
| `display` | string | `normal` `none` |
| `blendMode` | string | `normal` `multiply` `screen` `overlay` `darken` `lighten` |
| `blur` `backdropBlur` `brightness` `contrast` `saturation` `grayscale` `hueRotate` | float | number |
| `cursor` | string | `default` `pointer` `text` `grab` `grabbing` `resize` `crosshair` `notAllowed` |
| `left` `right` `top` `bottom` `centerX` `centerY` | string | anchor expression, such as `parent.left` |
| `direction` | string | `ltr` `rtl` `scrollX` `scrollY` `scrollBoth` `horizontal` `vertical` |
| `alignment` `distribution` `variant` | string | free text |
| `minScreen` `maxScreen` `minContainer` | float | number |
| `orientation` | string | free text |

`frame` also accepts every event.

Paint component C++ with solid fills, 1px borders, corner radius, clips, and text.

### `shadow`

Children: no. Place it inside a `frame` to add another shadow.

| Property | Type |
| --- | --- |
| `shadow` | string |
| `shadowColor` | color |
| `shadowX` `shadowY` `shadowBlur` `shadowSpread` | float |

### Typography

`text`, `heading`, `display`, `subhead`, `body`, `content`, `caption`, `eyebrow`, and `label` are text. They accept children, so a text box can also be a horizontal, vertical, grid, wrap, or stack container. They accept every `frame` property, including width, height, min and max, `widthMode`, `heightMode`, padding, margin, grow, shrink, basis, gap, columns, rows, column, row, spans, position, anchor, background, border, radius, overflow, and the event attributes.

`width="fill"` and `width="parent"` use the parent width. `width="screen"` uses the viewport width. `height` accepts the same keywords. `width="100%"` is a percent of the parent.

The default font is Inter. `fontFamily` is a family name such as `Inter` or `Segoe UI`. `fontFile` is a `.ttf`, `.otf`, or `.ttc` path. A path in `fontFamily` is treated as a file.

Roles match the foundation scale. Below 735px the mobile size is used unless `fontSize` is set.

| Element | Role | Desktop / mobile | Weight | Tracking | Line height | Tone |
| --- | --- | --- | --- | --- | --- | --- |
| `display` or `heading level="display"` | display | 80 / 40 | 600 | -0.015 | 1.05 | primary |
| `heading` or `heading level="1"` | h1 | 56 / 32 | 600 | -0.015 | 1.07 | primary |
| `heading level="2"` | h2 | 40 / 28 | 600 | -0.01 | 1.1 | primary |
| `heading level="3"` | h3 | 28 / 21 | 600 | -0.005 | 1.14 | primary |
| `subhead` | subhead | 24 / 19 | 400 | 0 | 1.33 | secondary |
| `body` `content` `text` | content | 19 / 17 | 400 | 0 | 1.42, or 1.47 on a phone | secondary |
| `body size="sm"` | content-sm | 17 | 400 | 0 | 1.47 | secondary |
| `caption` | caption | 12 | 400 | 0 | 1.33 | tertiary |
| `eyebrow` | eyebrow | 12 | 400 | 0.05 | 1.33 | tertiary, uppercase |
| `label` | label | 17 | 500 | 0 | 1 | primary |

`variant` sets the role directly: `display` `h1` `h2` `h3` `subhead` `content` `content-sm` `caption` `eyebrow` `label`. `tone` sets the color: `primary` `secondary` `tertiary` `disabled` `link` `danger` `success` `warning` `on-brand`. An explicit `fontSize`, `fontWeight`, `letterSpacing`, `lineHeight`, `textColor`, or `textTransform` replaces that part of the role.

`text` and `value` are the same string. Setting both is an error.

Text properties, in addition to the frame properties:

| Property | Type | Values |
| --- | --- | --- |
| `text` `value` `fontFamily` `fontFile` `fontFeatures` `language` | string | free text. `fontFile` is a font path |
| `fontSize` | font size | pixel length or a `$type` token |
| `fontWeight` | int | integer, or `$weight.regular` `$weight.medium` `$weight.semibold` |
| `fontStyle` | string | `normal` `italic` `oblique` |
| `letterSpacing` `wordSpacing` `lineHeight` `decorationWidth` | float | number. Tracking and line height from a role are em and a multiplier |
| `textColor` `decorationColor` | color | see Values |
| `textAlign` | string | `left` `start` `center` `right` `end` `justify` |
| `verticalAlign` | string | `top` `start` `center` `bottom` `end` |
| `wrap` | string | `wrap` `true` `nowrap` `false`. This wraps the text, not the flex line |
| `maxLines` | int | integer |
| `ellipsis` `underline` `lineThrough` `overline` | bool | `true` `false` |
| `overflow` | string | `visible` `hidden` `scroll` `auto` |
| `decorationStyle` | string | `solid` `dashed` `dotted` `double` `none` |
| `textTransform` | string | `none` `uppercase` `lowercase` `capitalize` |
| `direction` | string | `ltr` `rtl` `scrollX` `scrollY` `scrollBoth` `horizontal` `vertical` |
| `writingMode` | string | `horizontal` `vertical` |

### `image`

Children: no.

Identity properties, plus:

| Property | Type | Values |
| --- | --- | --- |
| `source` | string | path, such as `@assets/photo.png` |
| `fit` | string | `fill` `contain` `cover` `none` `scaleDown` |
| `radius` | length | see Values |
| `clip` `flipX` `flipY` | bool | `true` `false` |
| `tint` | color | see Values |
| `brightness` `contrast` `saturation` `rotation` | float | number |

### `vector`

Children: no.

Identity properties, plus:

| Property | Type | Values |
| --- | --- | --- |
| `shape` `transform` | string | free text |
| `fill` `stroke` | color | see Values |
| `strokeWidth` `strokeOpacity` | float | number |
| `lineCap` | string | `butt` `round` `square` |
| `lineJoin` | string | `miter` `round` `bevel` |
| `fillRule` | string | `nonzero` `evenodd` |
| `radius` | length | see Values |

### `path`

Children: no.

Identity properties, plus:

| Property | Type | Values |
| --- | --- | --- |
| `d` `dash` | string | `d` is path data |
| `fill` `stroke` | color | see Values |
| `strokeWidth` `dashOffset` | float | number |
| `lineCap` | string | `butt` `round` `square` |
| `lineJoin` | string | `miter` `round` `bevel` |

### `line`

Children: no.

Identity properties, plus:

| Property | Type | Values |
| --- | --- | --- |
| `x1` `y1` `x2` `y2` `strokeWidth` `dashGap` | float | number |
| `stroke` | color | see Values |
| `lineCap` | string | `butt` `round` `square` |
| `dash` | string | free text |

### `group`

Children: yes. A group organizes objects. It does not apply layout. That is what `frame` is for.

Identity properties, plus:

| Property | Type | Values |
| --- | --- | --- |
| `transform` | string | free text |
| `rotation` `scale` | float | number |
| `visibility` | string | `visible` `hidden` |

### `scroll`

Children: yes. Accepts every `frame` property, plus:

| Property | Type | Values |
| --- | --- | --- |
| `direction` | string | `ltr` `rtl` `scrollX` `scrollY` `scrollBoth` `horizontal` `vertical` |
| `offset` | float | number |
| `contentWidth` `contentHeight` | length | see Values |
| `scrollbar` `scrollbarVisibility` | string | `auto` `visible` `hidden` |
| `overscroll` | string | `auto` `contain` `none` |
| `scrollPhysics` | string | `clamping` `bouncing` |
| `smoothScrolling` | bool | `true` `false` |

### `component`, `instance`, and `slot`

`component` is the definition. `instance` uses it. `slot` is a hole the instance fills. All three accept children and the identity properties.

`component` and `instance` also accept `variant` (string). `instance` also accepts `component` (string), the definition name. `slot` also accepts `text` (string).

```xml
<component name="Button">
    <property name="label" type="string"/>
    <frame layout="horizontal" align="center" justify="center" padding="16" radius="8" cursor="pointer">
        <slot name="icon"/>
        <text value="{label}"/>
    </frame>
</component>
```

```xml
<instance component="Button" label="Save" onClick="save">
    <slot name="icon">
        <image source="@assets/save.png" width="16" height="16"/>
    </slot>
</instance>
```

### Events

These attributes are on `frame`, `scroll`, and every element that has the identity properties. The value is an event name or an action.

`onPointerDown` `onPointerUp` `onPointerMove` `onPointerEnter` `onPointerLeave` `onPointerOver` `onPointerOut` `onPointerCancel` `onClick` `onDoubleClick` `onContextMenu`

`onTouchStart` `onTouchMove` `onTouchEnd` `onTouchCancel`

`onKeyDown` `onKeyUp` `onKeyPress`

`onFocus` `onBlur` `onFocusIn` `onFocusOut`

`onInput` `onChange` `onBeforeInput` `onCompositionStart` `onCompositionUpdate` `onCompositionEnd`

`onSubmit` `onReset` `onValidate` `onInvalid`

`onDragStart` `onDrag` `onDragEnter` `onDragOver` `onDragLeave` `onDrop` `onDragEnd`

`onScroll` `onScrollStart` `onScrollEnd`

`onCreate` `onMount` `onUnmount` `onDestroy` `onLoad` `onUnload`

`onResize` `onLayout` `onPositionChange` `onSizeChange`

`onAnimationStart` `onAnimationUpdate` `onAnimationComplete` `onAnimationCancel`

`onStateChange` `onPropertyChange` `onVariantChange`

`onSelect` `onDeselect` `onSelectionChange`

`onLoadStart` `onLoadError` `onPlay` `onPause` `onEnded` `onTimeUpdate`

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

The class inherits `Element`. `Kind` returns the catalog kind. `Measure` reports the preferred size in DIPs. `Arrange` places this node and its children. `OnPaint` draws only this node's own pixels. Children are painted by the element base. Hit testing, hover, press, and key input are overrides on `Element` when the component is interactive.

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
2. Describe the visual tree and the public properties in the XML file, using only the elements and values in this document.
3. Declare the class, the property setters, and the `Element` overrides in the header.
4. Implement measure, arrange, and paint in the cpp file.
5. Register the element name, its properties, and their types in the component catalog so the compiler accepts the XML.
6. Teach `Instantiate` to construct the class from the compiled node.
7. Add a document that uses the component and a test that compiles it and checks one laid-out result.

Copy `Frame` when the new object is mostly a container. Copy a leaf such as `text` when it has no children. Slots are the extension point for content the instance supplies. Variants are properties, not extra classes.
