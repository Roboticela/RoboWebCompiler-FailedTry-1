#pragma once

#include "components/line/LineElement.h"

#include <cstdint>
#include <string>

namespace Runtime {

// An arrow is a line that wears a head. Everything a line is made of belongs to the line
// underneath it: the endpoints, the box their span produces, the stroke, the cap, the dash,
// the turn, the hit band, and the bindings that drive them. This class adds only the shape of
// the head and stands it on an end of the stroke.
//
// It gets that for free by overriding the stroke the line paints and calling straight back
// into the line to paint the shaft first, so the shaft of every arrow is literally the line's
// own stroke. A change to how a line measures, places, hits, or strokes itself therefore
// reaches every arrow without an arrow being touched.
class ArrowElement : public LineElement {
public:
    NodeKind Kind() const override;
    void ApplyBound(const std::string& property, const std::string& value) override;

    void SetHeadLength(float value);
    void SetHeadWidth(float value);
    void SetHead(std::string value);

    float HeadLength() const { return headLength; }
    float HeadWidth() const { return headWidth; }
    const std::string& Head() const { return headName; }

protected:
    void PaintStroke(Renderer& renderer, const Rect& area, Point start, Point end) override;

private:
    // Where the head goes. An arrow with a head at each end points both ways.
    enum class HeadMode : std::uint8_t {
        END,
        START,
        BOTH,
    };

    // One head, with its tip at the end of a stroke running along (directionX, directionY).
    void PaintHead(Renderer& renderer, const Rect& area, Point apex, float directionX, float directionY, float length, float half);

    float headLength = 12.0f;
    float headWidth = 12.0f;
    HeadMode head = HeadMode::END;
    std::string headName = "end";
};

}  // namespace Runtime
