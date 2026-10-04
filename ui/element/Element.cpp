#include "ui/element/Element.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <vector>

namespace Runtime {

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

Button* Element::AsButton() {
    return nullptr;
}

TextBlock* Element::AsText() {
    return nullptr;
}

bool Element::SetPointerHover(bool value) {
    if (pointerHover == value) {
        return false;
    }
    pointerHover = value;
    return true;
}

bool Element::SetPointerPressed(bool value) {
    if (pointerPressed == value) {
        return false;
    }
    pointerPressed = value;
    return true;
}

bool Element::OnPointerDown(Point) {
    return SetPointerPressed(true);
}

bool Element::OnDrag(Point) {
    return false;
}

bool Element::OnWheel(int) {
    return false;
}

void Element::Activate() {}

bool Element::WantsHandCursor() const {
    return false;
}

bool Element::TakesTextFocus() const {
    return false;
}

void Element::SetFocused(bool value) {
    pointerFocused = value;
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

Button* Element::FindFirstButton() {
    if (Button* button = AsButton()) {
        return button;
    }
    for (const std::unique_ptr<Element>& child : children) {
        if (Button* found = child->FindFirstButton()) {
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

void Element::AddBinding(std::string property, std::string key) {
    bindings.emplace_back(std::move(property), std::move(key));
}

void Element::SetAction(std::string value) {
    action = std::move(value);
}

void Element::SeedState(std::vector<std::pair<std::string, std::string>> states) {
    state.clear();
    for (const auto& entry : states) {
        state[entry.first] = entry.second;
    }
    ApplyState();
}

void Element::ApplyState() {
    Element* root = this;
    while (root->parent != nullptr) {
        root = root->parent;
    }
    for (const auto& binding : bindings) {
        const auto found = root->state.find(binding.second);
        ApplyBound(binding.first, found == root->state.end() ? std::string{} : found->second);
    }
    for (const std::unique_ptr<Element>& child : children) {
        child->ApplyState();
    }
}

void Element::ApplyBound(const std::string& property, const std::string& value) {
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
    if (!visible || (hideBelow > 0.0f && viewportWidth > 0.0f && viewportWidth < hideBelow)) {
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
