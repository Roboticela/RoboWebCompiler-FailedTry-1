#include "components/group/GroupElement.h"

#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace Runtime {
namespace {

std::string_view TrimView(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    const auto blank = [](char value) { return std::isspace(static_cast<unsigned char>(value)) != 0; };
    while (begin < end && blank(text[begin])) {
        ++begin;
    }
    while (end > begin && blank(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::vector<float> NumbersIn(std::string_view arguments) {
    std::vector<float> numbers;
    std::size_t index = 0;
    while (index < arguments.size()) {
        const char value = arguments[index];
        const bool start = std::isdigit(static_cast<unsigned char>(value)) != 0 || value == '-' || value == '+' || value == '.' ||
                           value == 'e' || value == 'E';
        if (!start) {
            ++index;
            continue;
        }
        const char* begin = arguments.data() + index;
        char* end = nullptr;
        const float number = std::strtof(begin, &end);
        if (end == begin) {
            ++index;
            continue;
        }
        numbers.push_back(number);
        index = static_cast<std::size_t>(end - arguments.data());
    }
    return numbers;
}

// Reads the transform shorthand: translate(x[, y]) scale(sx[, sy]) rotate(deg).
// Several verbs may appear in one string, and each one sets the matching part.
void ParseTransform(std::string_view text, float& translateX, float& translateY, float& scaleX, float& scaleY, float& rotation) {
    std::size_t index = 0;
    while (index < text.size()) {
        while (index < text.size() && (std::isalpha(static_cast<unsigned char>(text[index])) == 0)) {
            ++index;
        }
        const std::size_t verbStart = index;
        while (index < text.size() && std::isalpha(static_cast<unsigned char>(text[index])) != 0) {
            ++index;
        }
        if (verbStart == index) {
            break;
        }
        const std::string_view verb = text.substr(verbStart, index - verbStart);
        std::size_t open = index;
        while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open])) != 0) {
            ++open;
        }
        if (open >= text.size() || text[open] != '(') {
            continue;
        }
        const std::size_t close = text.find(')', open);
        if (close == std::string_view::npos) {
            break;
        }
        const std::vector<float> numbers = NumbersIn(text.substr(open + 1, close - open - 1));
        index = close + 1;
        if (numbers.empty()) {
            continue;
        }
        if (verb == "translate") {
            translateX = numbers[0];
            translateY = numbers.size() > 1 ? numbers[1] : 0.0f;
        } else if (verb == "scale") {
            scaleX = numbers[0];
            scaleY = numbers.size() > 1 ? numbers[1] : numbers[0];
        } else if (verb == "rotate") {
            rotation = numbers[0];
        }
    }
}

}  // namespace

NodeKind GroupElement::Kind() const { return NodeKind::GROUP; }

void GroupElement::SetName(std::string value) { name = std::move(value); }
const std::string& GroupElement::Name() const { return name; }
void GroupElement::SetRotation(float degrees) { rotationZ = degrees; }
float GroupElement::Rotation() const { return rotationZ; }
void GroupElement::SetScale(float x, float y) {
    scaleX = x;
    scaleY = y;
}
void GroupElement::SetTranslate(float x, float y) {
    translateX = x;
    translateY = y;
}
void GroupElement::SetTransform(std::string value) {
    transform = std::move(value);
    ParseTransform(transform, translateX, translateY, scaleX, scaleY, rotationZ);
}
const std::string& GroupElement::Transform() const { return transform; }
void GroupElement::SetClip(bool value) { clip = value; }
bool GroupElement::Clips() const { return clip; }
void GroupElement::SetCursor(std::string value) { cursor = std::move(value); }
const std::string& GroupElement::Cursor() const { return cursor; }

// A group paints nothing of its own, so hiding it can only mean hiding the whole group.
// An invisible node already skips measure, paint, and hit testing for its children.
void GroupElement::SetDisplay(std::string value) {
    const std::string text = std::string(TrimView(value));
    SetVisible(text != "none" && text != "hidden");
}

bool GroupElement::WantsHandCursor() const { return cursor == "pointer"; }

Size GroupElement::Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool) {
    // The group box covers the children, so the parent treats the whole group as one item.
    // Children keep the position they were given, which means wrapping a frame in a group
    // leaves it exactly where it was. Scale grows the box, which keeps the space the group
    // claims honest. The size the children cover together is kept as the size the group was
    // authored at, which is what Arrange scales the children out of.
    natural = ChildrenExtent(measurer, availableWidth, availableHeight);

    float resolvedWidth = natural.width * std::fabs(scaleX);
    float resolvedHeight = natural.height * std::fabs(scaleY);
    if (Width().unit == LengthUnit::PIXELS || Width().unit == LengthUnit::PERCENT || Width().unit == LengthUnit::FILL) {
        resolvedWidth = Width().Resolve(availableWidth, resolvedWidth);
    }
    if (Height().unit == LengthUnit::PIXELS || Height().unit == LengthUnit::PERCENT || Height().unit == LengthUnit::FILL) {
        resolvedHeight = Height().Resolve(availableHeight, resolvedHeight);
    }
    measured = Size{std::max(1.0f, resolvedWidth), std::max(1.0f, resolvedHeight)};
    return FinishMeasure(availableWidth, availableHeight);
}

void GroupElement::Arrange(const Rect& rect) {
    Element::Arrange(rect);
    bounds.x += translateX;
    bounds.y += translateY;

    // Children scale with the group box, so growing or shrinking the group moves and
    // resizes all of them together instead of only moving the box around them.
    ArrangeChildrenScaled(bounds, natural);
}

Element* GroupElement::HitTest(Point point) {
    if (!visible) {
        return nullptr;
    }
    if (clip && !ContainsPoint(bounds, point)) {
        return nullptr;
    }
    // A child keeps its own interaction, and the empty area of the group belongs to the
    // group, so the group is selected and moved as one.
    if (Element* hit = HitTestChildren(point)) {
        return hit;
    }
    return ContainsPoint(bounds, point) ? this : nullptr;
}

void GroupElement::OnPaint(Renderer& renderer) {
    const bool turned = std::fabs(rotationZ) > 0.01f;
    if (turned) {
        renderer.PushRotation(rotationZ, bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f);
    }
    if (clip) {
        renderer.PushClip(bounds);
    }
    PaintChildren(renderer);
    if (clip) {
        renderer.PopClip();
    }
    if (turned) {
        renderer.PopTransform();
    }
}

}  // namespace Runtime
