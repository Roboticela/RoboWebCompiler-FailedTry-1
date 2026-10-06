#pragma once

#include "engine/math/Geometry.h"
#include "ui/element/Element.h"

#include <string>

namespace Runtime {

// A group combines several children into one node.
//
// The parent lays a group out as a single item: its size is the union of its children
// unless an explicit width or height overrides it. Everything the group carries reaches
// every child, so one change lands on the whole group: opacity, visible, rotation,
// scale, translate, clip, cursor, z index, margin, grow, and the min and max caps.
//
// A group never applies layout of its own. Children keep their own x and y, expressed
// inside the group box, which is what a frame exists for.
class GroupElement : public Element {
public:
    NodeKind Kind() const override;
    Size Measure(TextMeasurer& measurer, float availableWidth, float availableHeight, bool heightDefinite) override;
    void Arrange(const Rect& rect) override;
    Element* HitTest(Point point) override;
    bool WantsHandCursor() const override;

    void SetName(std::string value);
    const std::string& Name() const;
    void SetRotation(float degrees);
    float Rotation() const;
    void SetScale(float x, float y);
    void SetTranslate(float x, float y);
    void SetTransform(std::string value);
    const std::string& Transform() const;
    void SetClip(bool value);
    bool Clips() const;
    void SetCursor(std::string value);
    const std::string& Cursor() const;
    void SetDisplay(std::string value);

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string name;
    std::string cursor;
    std::string transform;
    float rotationZ = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float translateX = 0.0f;
    float translateY = 0.0f;
    bool clip = false;
    Size natural{};
};

}  // namespace Runtime
