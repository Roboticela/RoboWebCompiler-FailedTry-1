#include "rendering/renderer/OutlineRecorder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace Runtime {
namespace {

constexpr float PI = 3.14159265358979f;
// How much of a line of text is ink. A string contributes its box rather than the outlines of its
// glyphs, so this is only used to answer a measurement asked during a paint pass.
constexpr float TEXT_INK_SHARE = 0.55f;

// A box with one radius on every corner, written in box-local pixels: four lines and four arcs, and
// a corner with no radius is a line that reaches the corner itself. A radius of half the box is a
// circle drawn this way, which is the shape the platform draws for a rounded rectangle that round.
std::string RoundedBoxPath(const Rect& box, float radius) {
    const float limit = std::min(box.width, box.height) * 0.5f;
    const float corner = std::clamp(radius, 0.0f, limit);
    const auto number = [](float value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f", value);
        return std::string(buffer);
    };
    if (corner <= 0.0f) {
        return "M 0.000 0.000 L " + number(box.width) + " 0.000 L " + number(box.width) + " " + number(box.height) +
               " L 0.000 " + number(box.height) + " Z";
    }
    // Clockwise from the top-left corner: top edge, right edge, bottom edge, left edge, each corner
    // an arc between the two lines that stop short of it.
    std::string path = "M " + number(corner) + " 0.000 ";
    path += "L " + number(box.width - corner) + " 0.000 ";
    path += "A " + number(corner) + " " + number(corner) + " 0 0 1 " + number(box.width) + " " + number(corner) + " ";
    path += "L " + number(box.width) + " " + number(box.height - corner) + " ";
    path += "A " + number(corner) + " " + number(corner) + " 0 0 1 " + number(box.width - corner) + " " + number(box.height) + " ";
    path += "L " + number(corner) + " " + number(box.height) + " ";
    path += "A " + number(corner) + " " + number(corner) + " 0 0 1 0.000 " + number(box.height - corner) + " ";
    path += "L 0.000 " + number(corner) + " ";
    path += "A " + number(corner) + " " + number(corner) + " 0 0 1 " + number(corner) + " 0.000 Z";
    return path;
}

std::string BoxPath(const Rect& box) {
    return RoundedBoxPath(box, 0.0f);
}

}  // namespace

OutlineRecorder::Transform OutlineRecorder::RotationAbout(float degrees, Point center) {
    const float radians = degrees * PI / 180.0f;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    OutlineRecorder::Transform transform;
    transform.m00 = cosine;
    transform.m01 = sine;
    transform.m10 = -sine;
    transform.m11 = cosine;
    transform.tx = center.x - cosine * center.x + sine * center.y;
    transform.ty = center.y - sine * center.x - cosine * center.y;
    return transform;
}

void OutlineRecorder::SetDpi(float) {}

void OutlineRecorder::SetTransform(float, float, float) {
    // The transform a frame is presented under is the page's own pan and zoom. The outlines a shape
    // covers are read where the shape was placed, not where the window is showing it.
}

void OutlineRecorder::BeginFrame(Color) {}
bool OutlineRecorder::EndFrame() { return true; }
void OutlineRecorder::PushClip(const Rect&) {}
void OutlineRecorder::PopClip() {}
void OutlineRecorder::PushOpacity(float) {}
void OutlineRecorder::PopOpacity() {}

void OutlineRecorder::SetOrigin(Point point) {
    origin = point;
}

void OutlineRecorder::PushRotation(float degrees, float centerX, float centerY) {
    // A turn reaches every shape the node holds, so it is composed onto the transform already in
    // force rather than replacing it.
    Transform composed = RotationAbout(degrees, Point{centerX, centerY});
    if (!transforms.empty()) {
        const Transform& current = transforms.back();
        Transform applied;
        applied.m00 = composed.m00 * current.m00 + composed.m10 * current.m01;
        applied.m01 = composed.m01 * current.m00 + composed.m11 * current.m01;
        applied.m10 = composed.m00 * current.m10 + composed.m10 * current.m11;
        applied.m11 = composed.m01 * current.m10 + composed.m11 * current.m11;
        applied.tx = composed.m00 * current.tx + composed.m10 * current.ty + composed.tx;
        applied.ty = composed.m01 * current.tx + composed.m11 * current.ty + composed.ty;
        composed = applied;
    }
    transforms.push_back(composed);
}

void OutlineRecorder::PopTransform() {
    if (!transforms.empty()) {
        transforms.pop_back();
    }
}

void OutlineRecorder::FillRectangle(const Rect& rect, const Color&, float radius) {
    Capture(RoundedBoxPath(rect, radius), rect, 0.0f, 0.0f, 0.0f, 0.0f);
}

void OutlineRecorder::StrokeRectangle(const Rect& rect, const Color&, float, float radius) {
    // A stroke sits along the outline, so the rectangle it was stroked around is the shape it draws.
    Capture(RoundedBoxPath(rect, radius), rect, 0.0f, 0.0f, 0.0f, 0.0f);
}

void OutlineRecorder::DrawString(std::string_view, const Rect& rect, const TextStyle&) {
    // The outlines of glyphs are not something this reads, so a string contributes the box it was
    // laid out in.
    Capture(BoxPath(rect), rect, 0.0f, 0.0f, 0.0f, 0.0f);
}

void OutlineRecorder::DrawCachedImage(std::string_view, const Rect& rect, const unsigned char*, int, int) {
    Capture(BoxPath(rect), rect, 0.0f, 0.0f, 0.0f, 0.0f);
}

void OutlineRecorder::DrawVector(const VectorPaint& paint) {
    Capture(paint.data, paint.bounds, paint.viewX, paint.viewY, paint.viewWidth, paint.viewHeight);
}

Size OutlineRecorder::MeasureText(std::string_view text, float fontSize, float maxWidth) {
    const float width = static_cast<float>(text.size()) * fontSize * TEXT_INK_SHARE;
    return Size{maxWidth > 0.0f ? std::min(width, maxWidth) : width, fontSize};
}

void OutlineRecorder::Capture(std::string_view data, const Rect& bounds, float viewX, float viewY, float viewWidth, float viewHeight) {
    if (data.empty() || bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return;
    }
    const std::vector<PathContour> contours = SampleContours(data);
    if (contours.empty()) {
        return;
    }
    // A path written in a view box is stretched into the box it was given, which is how an icon
    // written small is drawn large. Without a view box the path is already written in the box.
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    if (viewWidth > 0.0f && viewHeight > 0.0f) {
        scaleX = bounds.width / viewWidth;
        scaleY = bounds.height / viewHeight;
    } else {
        viewX = 0.0f;
        viewY = 0.0f;
    }
    const Transform identity;
    const Transform& transform = transforms.empty() ? identity : transforms.back();
    for (const PathContour& contour : contours) {
        PathContour placed;
        // An outline left open is closed before it is combined: what a shape covers is a region, and
        // a region is bounded by a loop.
        placed.closed = true;
        placed.points.reserve(contour.points.size() + 1);
        for (const PathSample& sample : contour.points) {
            const float boxX = (sample.x - viewX) * scaleX + bounds.x;
            const float boxY = (sample.y - viewY) * scaleY + bounds.y;
            const Point placedPoint = transform.Apply(Point{boxX, boxY});
            placed.points.push_back(PathSample{placedPoint.x - origin.x, placedPoint.y - origin.y});
        }
        if (placed.points.size() > 1) {
            outlines.push_back(std::move(placed));
        }
    }
}

}  // namespace Runtime
