#include "ui/element/Element.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <vector>

namespace Runtime {
namespace {
Element* RootOf(Element* element);
}
void Element::Arrange(const Rect& rect) {
    bounds = BorderBox(rect);
}

void Element::Paint(Renderer& renderer) {
    if (!visible || opacity <= 0.0f) {
        return;
    }
    renderer.PushOpacity(opacity);
    PaintChrome(renderer);
    OnPaint(renderer);
    if (pointerHover && !tip.empty()) {
        const float tipWidth = std::max(48.0f, static_cast<float>(tip.size()) * 7.0f);
        const float tipHeight = 28.0f;
        float tipY = bounds.y - tipHeight - 6.0f;
        if (tipY < 0.0f) {
            tipY = bounds.y + bounds.height + 6.0f;
        }
        const Rect tipRect{bounds.x, tipY, tipWidth, tipHeight};
        renderer.FillRectangle(tipRect, COLOR_TEXT_PRIMARY, RADIUS_SM);
        TextStyle style;
        style.fontSize = 12.0f;
        style.fontWeight = WEIGHT_MEDIUM;
        style.color = COLOR_TEXT_ON_BRAND;
        style.horizontal = TextAlign::CENTER;
        style.vertical = TextAlign::CENTER;
        renderer.DrawString(tip, tipRect, style);
    }
    renderer.PopOpacity();
}

void Element::OnPaint(Renderer& renderer) {
    PaintChildren(renderer);
}

void Element::PaintChrome(Renderer& renderer) {
    const StylePatch patch = ActivePatch();
    const bool drawBackground = chromeBackground || patch.background;
    const Color fill = patch.background ? patch.backgroundColor : background;
    const float radius = patch.radius ? patch.radiusLength.Resolve(std::min(bounds.width, bounds.height) * 0.5f, 0.0f)
                                      : chromeRadius.Resolve(std::min(bounds.width, bounds.height) * 0.5f, 0.0f);
    if (drawBackground && bounds.width > 0.0f && bounds.height > 0.0f) {
        renderer.FillRectangle(bounds, fill, radius);
    }
    const float thickness = patch.border ? patch.borderWidth : borderWidth;
    const Color stroke = patch.border ? patch.borderColor : borderColor;
    if (thickness > 0.0f && bounds.width > 0.0f && bounds.height > 0.0f) {
        renderer.StrokeRectangle(bounds, stroke, thickness, radius);
    }
}

Element* Element::HitTest(Point point) {
    if (!visible) {
        return nullptr;
    }
    return HitTestChildren(point);
}

bool Element::SetPointerHover(bool value) {
    if (pointerHover == value) {
        return false;
    }
    pointerHover = value;
    if (value) {
        OnPointerEnter();
    } else {
        OnPointerLeave();
    }
    return true;
}

bool Element::SetPointerPressed(bool value) {
    if (pointerPressed == value) {
        return false;
    }
    pointerPressed = value;
    if (!value) {
        OnPointerUp();
    }
    return true;
}

void Element::OnPointerEnter() {}

void Element::OnPointerLeave() {}

void Element::OnPointerDown() {}

void Element::OnPointerUp() {}

void Element::OnFocus() {}

void Element::OnBlur() {}

bool Element::OnPointerDown(Point) {
    OnPointerDown();
    return SetPointerPressed(true);
}

bool Element::OnDrag(Point) {
    return false;
}

bool Element::OnWheel(int) {
    return false;
}

void Element::Activate() {
    KeyEvent event;
    event.key = "Enter";
    event.code = "Enter";
    Emit("click", event);
    Element* root = RootOf(this);
    if (root != nullptr) {
        root->announcement = "activate " + AccessibleName();
    }
}

bool Element::WantsHandCursor() const {
    return false;
}

bool Element::TakesTextFocus() const {
    return false;
}

void Element::SetFocused(bool value) {
    const bool changed = pointerFocused != value;
    pointerFocused = value;
    if (!value) {
        if (changed) {
            OnBlur();
        }
        return;
    }
    if (changed) {
        OnFocus();
    }
    Element* root = RootOf(this);
    if (root != nullptr) {
        root->announcement = "focus " + AccessibleName();
    }
}

bool Element::OnChar(wchar_t) {
    return false;
}

bool Element::OnKeyDown(unsigned) {
    return false;
}

Element* Element::FindById(std::string_view target) {
    if (!id.empty() && id == target) {
        return this;
    }
    for (const std::unique_ptr<Element>& child : children) {
        if (Element* found = child->FindById(target)) {
            return found;
        }
    }
    return nullptr;
}

const std::string& Element::Id() const {
    return id;
}

void Element::SetId(std::string value) {
    id = std::move(value);
}

Length Element::Width() const {
    return width;
}

Length Element::Height() const {
    return height;
}

void Element::SetWidth(Length value) {
    width = value;
}

void Element::SetHeight(Length value) {
    height = value;
}

Rect Element::Bounds() const {
    return bounds;
}

Size Element::Measured() const {
    return measured;
}

int Element::ZIndex() const {
    return zIndex;
}

void Element::SetZIndex(int value) {
    zIndex = value;
}

Length Element::MinWidth() const {
    return minWidth;
}

Length Element::MaxWidth() const {
    return maxWidth;
}

Length Element::MinHeight() const {
    return minHeight;
}

Length Element::MaxHeight() const {
    return maxHeight;
}

void Element::SetMinWidth(Length value) {
    minWidth = value;
}

void Element::SetMaxWidth(Length value) {
    maxWidth = value;
}

void Element::SetMinHeight(Length value) {
    minHeight = value;
}

void Element::SetMaxHeight(Length value) {
    maxHeight = value;
}

Edges Element::Margin() const {
    return margin;
}

void Element::SetMargin(Edges value) {
    margin = value;
}

float Element::Opacity() const {
    return opacity;
}

void Element::SetOpacity(float value) {
    opacity = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

bool Element::Visible() const {
    return visible;
}

void Element::SetVisible(bool value) {
    visible = value;
}

Length Element::OffsetX() const {
    return offsetX;
}

Length Element::OffsetY() const {
    return offsetY;
}

void Element::SetOffsetX(Length value) {
    offsetX = value;
}

void Element::SetOffsetY(Length value) {
    offsetY = value;
}

FramePosition Element::PositionMode() const {
    return positionMode;
}

void Element::SetPositionMode(FramePosition value) {
    positionMode = value;
}

void Element::SetChrome(Color color, bool enabled) {
    background = color;
    chromeBackground = enabled;
}

void Element::SetBorder(float thickness, Color color) {
    borderWidth = thickness < 0.0f ? 0.0f : thickness;
    borderColor = color;
}

void Element::SetChromeRadius(Length value) {
    chromeRadius = value;
}

void Element::SetVisualStyle(const VisualStyle& style) {
    visual = style;
}

StylePatch Element::ActivePatch() const {
    StylePatch patch = visual.phases[STYLE_NORMAL];
    if (pointerFocused) {
        OverlayPatch(patch, visual.phases[STYLE_FOCUSED]);
    }
    if (pointerHover) {
        OverlayPatch(patch, visual.phases[STYLE_HOVER]);
    }
    if (pointerPressed) {
        OverlayPatch(patch, visual.phases[STYLE_PRESSED]);
    }
    const auto fits = [&](const StylePatch& over) {
        if (over.maxWidth > 0.0f && viewportWidth > over.maxWidth) {
            return false;
        }
        if (over.minWidth > 0.0f && viewportWidth > 0.0f && viewportWidth < over.minWidth) {
            return false;
        }
        return over.maxWidth > 0.0f || over.minWidth > 0.0f;
    };
    for (const auto& extra : visual.extraPhases) {
        if (fits(extra.second)) {
            OverlayPatch(patch, extra.second);
        }
    }
    return patch;
}

bool Element::HasVisualStyle() const {
    return visual.used;
}

float Element::Grow() const {
    return grow;
}

void Element::SetGrow(float value) {
    grow = value < 0.0f ? 0.0f : value;
}

int Element::ColumnSpan() const {
    return columnSpan < 1 ? 1 : columnSpan;
}

void Element::SetColumnSpan(int value) {
    columnSpan = value < 1 ? 1 : value;
}

void Element::SetTip(std::string value) {
    tip = std::move(value);
}

void Element::SetHiddenBelow(float value) {
    hideBelow = value;
}

void Element::SetViewport(float width, float height) {
    viewportWidth = width;
    viewportHeight = height;
    for (const std::unique_ptr<Element>& child : children) {
        child->SetViewport(width, height);
    }
}

float Element::ViewportWidth() const {
    return viewportWidth;
}

float Element::ViewportHeight() const {
    return viewportHeight;
}

void Element::AddBinding(std::string property, std::string key, BindMode mode, bool invert) {
    BoundLink link;
    link.property = std::move(property);
    link.key = std::move(key);
    link.mode = mode;
    link.invert = invert;
    bindings.push_back(std::move(link));
}

void Element::SetEdgePin(BoxEdge edge, float offset) {
    pins[static_cast<int>(edge)] = EdgePin{true, offset};
}

EdgePin Element::EdgePinOf(BoxEdge edge) const {
    return pins[static_cast<int>(edge)];
}

Rect Element::PinnedRect(const Rect& parent, const Rect& natural) const {
    const EdgePin left = EdgePinOf(BoxEdge::LEFT);
    const EdgePin right = EdgePinOf(BoxEdge::RIGHT);
    const EdgePin top = EdgePinOf(BoxEdge::TOP);
    const EdgePin bottom = EdgePinOf(BoxEdge::BOTTOM);
    const EdgePin centerX = EdgePinOf(BoxEdge::CENTER_X);
    const EdgePin centerY = EdgePinOf(BoxEdge::CENTER_Y);
    const bool any = left.used || right.used || top.used || bottom.used || centerX.used || centerY.used;
    if (!any) {
        return natural;
    }
    float x = natural.x;
    float y = natural.y;
    float width = natural.width;
    float height = natural.height;
    if (left.used && right.used) {
        x = parent.x + left.offset;
        width = std::max(0.0f, parent.x + parent.width - right.offset - x);
    } else if (left.used) {
        x = parent.x + left.offset;
    } else if (right.used) {
        x = parent.x + parent.width - width - right.offset;
    }
    if (centerX.used) {
        x = parent.x + (parent.width - width) * 0.5f + centerX.offset;
    }
    if (top.used && bottom.used) {
        y = parent.y + top.offset;
        height = std::max(0.0f, parent.y + parent.height - bottom.offset - y);
    } else if (top.used) {
        y = parent.y + top.offset;
    } else if (bottom.used) {
        y = parent.y + parent.height - height - bottom.offset;
    }
    if (centerY.used) {
        y = parent.y + (parent.height - height) * 0.5f + centerY.offset;
    }
    return Rect{x, y, width, height};
}

void Element::SetMaxScreen(float value) {
    hideAbove = value;
}

void Element::SetOrientation(std::string value) {
    orientation = std::move(value);
}

void Element::SetMinContainer(float value) {
    minContainer = value;
}

int Element::ApplyCount() const {
    return applyCount;
}

void Element::Connect(std::string_view eventName, std::function<void(const KeyEvent&)> handler) {
    handlers[std::string(eventName)] = std::move(handler);
}

void Element::Emit(std::string_view eventName, const KeyEvent& event) {
    const auto found = handlers.find(std::string(eventName));
    if (found != handlers.end() && found->second) {
        found->second(event);
    }
}

bool Element::OnKeyEvent(const KeyEvent& event) {
    if (event.key == "Enter" || event.key == "Space") {
        Emit("click", event);
        Element* root = RootOf(this);
        if (root != nullptr) {
            root->announcement = "activate " + AccessibleName();
        }
        return true;
    }
    Emit("keyDown", event);
    return handlers.find("keyDown") != handlers.end();
}

void Element::SetAction(std::string value) {
    action = std::move(value);
}

const std::string& Element::Action() const {
    return action;
}

namespace {

void CollectFocus(Element* node, std::vector<Element*>& order) {
    if (node == nullptr || !node->Visible()) {
        return;
    }
    const bool focusable = !node->Action().empty() || node->TakesTextFocus() || node->AccessibleName().size() > 0;
    if (focusable && node->Parent() != nullptr) {
        order.push_back(node);
    }
    for (const std::unique_ptr<Element>& child : node->Children()) {
        CollectFocus(child.get(), order);
    }
}

}  // namespace

Element* Element::NextFocus(Element* current, bool reverse) {
    std::vector<Element*> order;
    CollectFocus(this, order);
    if (order.empty()) {
        return nullptr;
    }
    int index = -1;
    for (int cursor = 0; cursor < static_cast<int>(order.size()); ++cursor) {
        if (order[static_cast<std::size_t>(cursor)] == current) {
            index = cursor;
        }
    }
    if (current != nullptr) {
        current->SetFocused(false);
    }
    const int next = reverse ? (index <= 0 ? static_cast<int>(order.size()) - 1 : index - 1)
                             : (index + 1) % static_cast<int>(order.size());
    order[static_cast<std::size_t>(next)]->SetFocused(true);
    return order[static_cast<std::size_t>(next)];
}

std::string Element::AccessibleName() const {
    if (!accessibleName.empty()) {
        return accessibleName;
    }
    if (!tip.empty()) {
        return tip;
    }
    return id;
}

void Element::SetAccessibleName(std::string value) {
    accessibleName = std::move(value);
}

std::string Element::AccessibilityState() const {
    std::string value;
    if (pointerFocused) {
        value += "focused ";
    }
    if (accessDisabled) {
        value += "disabled ";
    }
    if (accessSelected) {
        value += "selected ";
    }
    if (accessExpanded) {
        value += "expanded ";
    }
    return value;
}

void Element::SetAccessibilityFlag(std::string_view name, bool value) {
    if (name == "disabled" || name == "readonly") {
        accessDisabled = value;
    } else if (name == "selected" || name == "checked") {
        accessSelected = value;
    } else if (name == "expanded") {
        accessExpanded = value;
    }
    Element* root = RootOf(this);
    if (root != nullptr) {
        root->announcement = "state " + AccessibilityState();
    }
}

const std::string& Element::Announcement() const {
    return announcement;
}

void Element::SetMotions(std::vector<MotionDecl> value) {
    motions = std::move(value);
}

bool Element::Tick(float seconds) {
    motionTime += seconds;
    bool changed = false;
    const std::vector<MotionSample> samples = SampleMotions(motions, motionTime, 0, pointerHover);
    for (const MotionSample& sample : samples) {
        Element* target = sample.target.empty() ? this : FindById(sample.target);
        if (target == nullptr || !sample.hasNumber) {
            continue;
        }
        changed = true;
        if (sample.property == "opacity") {
            target->SetOpacity(sample.number);
        } else if (sample.property == "x") {
            target->SetOffsetX(Length::Pixels(sample.number));
        } else if (sample.property == "y") {
            target->SetOffsetY(Length::Pixels(sample.number));
        }
    }
    for (const std::unique_ptr<Element>& child : children) {
        changed = child->Tick(seconds) || changed;
    }
    return changed;
}

void Element::SeedState(std::vector<std::pair<std::string, std::string>> states) {
    state.clear();
    for (const auto& entry : states) {
        state[entry.first] = entry.second;
    }
    ApplyState();
}

namespace {

Element* RootOf(Element* element) {
    while (element != nullptr && element->Parent() != nullptr) {
        element = element->Parent();
    }
    return element;
}

}  // namespace

std::string Element::ReadState(const std::string& key) const {
    const Element* root = this;
    while (root->parent != nullptr) {
        root = root->parent;
    }
    const auto found = root->state.find(key);
    return found == root->state.end() ? std::string{} : found->second;
}

void Element::UpdateKey(const std::string& key) {
    Element* root = RootOf(this);
    for (BoundLink& link : bindings) {
        const bool matches = link.key == key || link.key.find(key) != std::string::npos;
        if (!matches) {
            continue;
        }
        if (link.mode == BindMode::ONE_TIME && link.applied) {
            continue;
        }
        if (link.mode == BindMode::ASYNC && root != nullptr && root->state.find(link.key) == root->state.end()) {
            continue;
        }
        std::string resolved;
        if (link.mode == BindMode::COMPUTED) {
            std::string part;
            for (std::size_t index = 0; index <= link.key.size(); ++index) {
                if (index == link.key.size() || link.key[index] == '+') {
                    if (!part.empty()) {
                        if (!resolved.empty()) {
                            resolved += " ";
                        }
                        resolved += ReadState(part);
                        part.clear();
                    }
                } else if (link.key[index] != ' ') {
                    part.push_back(link.key[index]);
                }
            }
        } else {
            resolved = ReadState(link.key);
        }
        if (link.invert) {
            const bool enabled = resolved == "true" || resolved == "1";
            resolved = enabled ? "false" : "true";
        }
        ApplyBound(link.property, resolved);
        link.applied = true;
    }
    for (const std::unique_ptr<Element>& child : children) {
        child->UpdateKey(key);
    }
}

void Element::ApplyState() {
    Element* root = RootOf(this);
    if (root != nullptr && root != this) {
        root->ApplyState();
        return;
    }
    std::vector<std::string> keys;
    for (const auto& entry : state) {
        keys.push_back(entry.first);
    }
    const auto collect = [&](auto&& self, Element* node) -> void {
        for (const BoundLink& link : node->bindings) {
            keys.push_back(link.key);
        }
        for (const std::unique_ptr<Element>& child : node->children) {
            self(self, child.get());
        }
    };
    collect(collect, this);
    for (const std::string& key : keys) {
        UpdateKey(key);
    }
}

void Element::SetStateValue(const std::string& key, const std::string& value) {
    Element* root = RootOf(this);
    if (root == nullptr) {
        return;
    }
    root->state[key] = value;
    root->UpdateKey(key);
}

void Element::WriteBinding(const std::string& property, const std::string& value) {
    for (const BoundLink& link : bindings) {
        if (link.property == property && (link.mode == BindMode::TWO_WAY || link.mode == BindMode::REACTIVE)) {
            SetStateValue(link.key, value);
            return;
        }
    }
}

void Element::ApplyBound(const std::string& property, const std::string& value) {
    ++applyCount;
    if (property == "visible" || property == "visibility") {
        SetVisible(value != "false" && value != "hidden" && value != "0");
    }
}

bool Element::Submit() {
    bool ok = true;
    for (const std::unique_ptr<Element>& child : children) {
        ok = child->Submit() && ok;
    }
    return ok;
}

namespace {

std::string ActionArgument(const std::string& action, std::string_view prefix) {
    return action.substr(prefix.size());
}

}  // namespace

void Element::RunAction(const std::string& command) {
    Element* root = this;
    while (root->parent != nullptr) {
        root = root->parent;
    }
    if (command.rfind("show:", 0) == 0) {
        if (Element* target = root->FindById(ActionArgument(command, "show:"))) {
            target->SetVisible(true);
            root->InvalidateLayout();
        }
        return;
    }
    if (command.rfind("hide:", 0) == 0) {
        if (Element* target = root->FindById(ActionArgument(command, "hide:"))) {
            target->SetVisible(false);
            root->InvalidateLayout();
        }
        return;
    }
    if (command.rfind("toggle:", 0) == 0) {
        if (Element* target = root->FindById(ActionArgument(command, "toggle:"))) {
            target->SetVisible(!target->Visible());
            root->InvalidateLayout();
        }
        return;
    }
    if (command.rfind("submit:", 0) == 0) {
        if (Element* target = root->FindById(ActionArgument(command, "submit:"))) {
            target->Submit();
        }
        return;
    }
    if (command.rfind("set:", 0) == 0) {
        const std::string body = ActionArgument(command, "set:");
        const std::size_t eq = body.find('=');
        if (eq == std::string::npos) {
            return;
        }
        root->state[body.substr(0, eq)] = body.substr(eq + 1);
        root->ApplyState();
        root->InvalidateLayout();
    }
}

Element* Element::Parent() const {
    return parent;
}

void Element::InvalidateLayout() {
    layoutDirty = true;
    if (parent != nullptr) {
        parent->InvalidateLayout();
    }
}

bool Element::TakeLayoutDirty() {
    const bool value = layoutDirty;
    layoutDirty = false;
    return value;
}

Size Element::FinishMeasure(float availableWidth, float availableHeight) {
    const bool narrow = hideBelow > 0.0f && viewportWidth > 0.0f && viewportWidth < hideBelow;
    const bool wide = hideAbove > 0.0f && viewportWidth > hideAbove;
    const bool portrait = orientation == "portrait" && viewportWidth > 0.0f && viewportWidth > viewportHeight;
    const bool landscape = orientation == "landscape" && viewportHeight > 0.0f && viewportHeight > viewportWidth;
    const bool container = minContainer > 0.0f && availableWidth > 0.0f && availableWidth < minContainer;
    if (!visible || narrow || wide || portrait || landscape || container) {
        measured = {};
        return measured;
    }
    measured.width = ClampAxis(measured.width, minWidth, maxWidth, availableWidth);
    measured.height = ClampAxis(measured.height, minHeight, maxHeight, availableHeight);
    measured.width += margin.SpanX(availableWidth);
    measured.height += margin.SpanY(availableHeight);
    return measured;
}

Rect Element::BorderBox(const Rect& outer) const {
    const float left = margin.left.Resolve(outer.width, 0.0f);
    const float right = margin.right.Resolve(outer.width, 0.0f);
    const float top = margin.top.Resolve(outer.height, 0.0f);
    const float bottom = margin.bottom.Resolve(outer.height, 0.0f);
    return Rect{outer.x + left, outer.y + top, std::max(0.0f, outer.width - left - right),
                std::max(0.0f, outer.height - top - bottom)};
}

void Element::AddChild(std::unique_ptr<Element> child) {
    child->parent = this;
    children.push_back(std::move(child));
}

const std::vector<std::unique_ptr<Element>>& Element::Children() const {
    return children;
}

void Element::PaintChildren(Renderer& renderer) {
    std::vector<Element*> order;
    order.reserve(children.size());
    for (const std::unique_ptr<Element>& child : children) {
        order.push_back(child.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Element* left, const Element* right) { return left->ZIndex() < right->ZIndex(); });
    for (Element* child : order) {
        child->Paint(renderer);
    }
}

Element* Element::HitTestChildren(Point point) {
    std::vector<Element*> order;
    order.reserve(children.size());
    for (const std::unique_ptr<Element>& child : children) {
        order.push_back(child.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Element* left, const Element* right) { return left->ZIndex() < right->ZIndex(); });
    for (auto child = order.rbegin(); child != order.rend(); ++child) {
        if (Element* hit = (*child)->HitTest(point)) {
            return hit;
        }
    }
    return nullptr;
}

}  // namespace Runtime
