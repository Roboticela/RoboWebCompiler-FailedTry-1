#include "rendering/path/PathBoolean.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Runtime {
namespace {

// Two points this close together are the same point. A boundary that was split where another
// boundary crossed it is written down twice, once from each side, and the two readings differ by
// less than this, so this is what lets the pieces meet again when the loops are assembled.
constexpr float POINT_EPSILON = 0.001f;
// A loop that covers less than this covers nothing: a closed line has no area, and a shape that was
// asked for a size of zero is not a part of a combination.
constexpr float DEGENERATE_AREA = 0.001f;
// How far off a boundary the space on each side of it is read, as a share of the size of the whole
// combination.
constexpr float PROBE_SCALE = 0.001f;
constexpr float MIN_PROBE = 0.0001f;
// A probe never reaches past this share of the piece it is reading, so a piece shorter than the
// probe is still read on the side of itself it is on.
constexpr float MAX_PROBE_SHARE = 0.25f;
// How close two pieces' ends have to be to be the same end.
constexpr float JOIN_TOLERANCE = 0.01f;

struct Segment {
    Point a;
    Point b;
};

struct Piece {
    Point a;
    Point b;
};

float Side(Point a, Point b, Point p) {
    return (b.x - a.x) * (p.y - a.y) - (p.x - a.x) * (b.y - a.y);
}

bool SamePoint(Point left, Point right, float tolerance = POINT_EPSILON) {
    return std::fabs(left.x - right.x) <= tolerance && std::fabs(left.y - right.y) <= tolerance;
}

Point PointAt(const Segment& segment, float t) {
    return Point{segment.a.x + (segment.b.x - segment.a.x) * t, segment.a.y + (segment.b.y - segment.a.y) * t};
}

float SignedArea(const std::vector<Point>& loop) {
    float total = 0.0f;
    for (std::size_t index = 0; index < loop.size(); ++index) {
        const Point& current = loop[index];
        const Point& next = loop[(index + 1) % loop.size()];
        total += current.x * next.y - next.x * current.y;
    }
    return total * 0.5f;
}

// The points of one outline, with the point that closes it folded away: a loop is written by naming
// each point once, and the way back to the first is what closes it.
std::vector<Point> LoopPoints(const PathContour& outline) {
    std::vector<Point> points;
    points.reserve(outline.points.size());
    for (const PathSample& sample : outline.points) {
        const Point point{sample.x, sample.y};
        if (points.empty() || !SamePoint(points.back(), point)) {
            points.push_back(point);
        }
    }
    if (points.size() > 1 && SamePoint(points.front(), points.back())) {
        points.pop_back();
    }
    return points;
}

// The winding number of a point around a set of segments: how many times the outline travels
// around the point, counted one way as positive and the other as negative. Zero is outside.
int WindingAt(const std::vector<Segment>& segments, Point point) {
    int winding = 0;
    for (const Segment& segment : segments) {
        if (segment.a.y <= point.y) {
            if (segment.b.y > point.y && Side(segment.a, segment.b, point) > 0.0f) {
                ++winding;
            }
        } else if (segment.b.y <= point.y && Side(segment.a, segment.b, point) < 0.0f) {
            --winding;
        }
    }
    return winding;
}

bool LoopContains(const std::vector<Point>& loop, Point point) {
    std::vector<Segment> segments;
    segments.reserve(loop.size());
    for (std::size_t index = 0; index < loop.size(); ++index) {
        segments.push_back(Segment{loop[index], loop[(index + 1) % loop.size()]});
    }
    return WindingAt(segments, point) != 0;
}

// One shape, read into the segments its boundary is made of. The shape covers the points its
// winding number is not zero at, so a loop nested inside another loop is a hole in it: it is wound
// the other way round, and the two together come to nothing inside the hole. Winding every loop
// alike would fill the hole in instead, which is what a ring drawn as two circles is not.
std::vector<Segment> ReadShape(const std::vector<PathContour>& outlines) {
    std::vector<std::vector<Point>> loops;
    for (const PathContour& outline : outlines) {
        std::vector<Point> loop = LoopPoints(outline);
        if (loop.size() < 3 || std::fabs(SignedArea(loop)) < DEGENERATE_AREA) {
            continue;
        }
        loops.push_back(std::move(loop));
    }
    std::vector<Segment> segments;
    for (std::size_t index = 0; index < loops.size(); ++index) {
        int depth = 0;
        for (std::size_t other = 0; other < loops.size(); ++other) {
            if (other != index && LoopContains(loops[other], loops[index].front())) {
                ++depth;
            }
        }
        std::vector<Point> loop = loops[index];
        const bool solid = depth % 2 == 0;
        if ((SignedArea(loop) > 0.0f) != solid) {
            std::reverse(loop.begin(), loop.end());
        }
        for (std::size_t step = 0; step < loop.size(); ++step) {
            segments.push_back(Segment{loop[step], loop[(step + 1) % loop.size()]});
        }
    }
    return segments;
}

bool ShapeCovers(const std::vector<Segment>& shape, Point point) {
    return WindingAt(shape, point) != 0;
}

// Which shapes cover this point, read through the operation the group was given.
bool ResultCovers(const std::vector<std::vector<Segment>>& shapes, BooleanOperation operation, Point point) {
    if (shapes.empty()) {
        return false;
    }
    switch (operation) {
    case BooleanOperation::UNION: {
        for (const std::vector<Segment>& shape : shapes) {
            if (ShapeCovers(shape, point)) {
                return true;
            }
        }
        return false;
    }
    case BooleanOperation::SUBTRACT: {
        if (!ShapeCovers(shapes.front(), point)) {
            return false;
        }
        for (std::size_t index = 1; index < shapes.size(); ++index) {
            if (ShapeCovers(shapes[index], point)) {
                return false;
            }
        }
        return true;
    }
    case BooleanOperation::INTERSECT: {
        for (const std::vector<Segment>& shape : shapes) {
            if (!ShapeCovers(shape, point)) {
                return false;
            }
        }
        return true;
    }
    case BooleanOperation::EXCLUDE: {
        int covering = 0;
        for (const std::vector<Segment>& shape : shapes) {
            if (ShapeCovers(shape, point)) {
                ++covering;
            }
        }
        return covering % 2 == 1;
    }
    }
    return false;
}

void AddCut(std::vector<float>& cuts, float t) {
    // A cut at either end of a segment divides nothing: the end is already a cut.
    if (t > 1e-5f && t < 1.0f - 1e-5f) {
        cuts.push_back(t);
    }
}

// Where another segment crosses this one, written as a distance along it. Two segments that run
// along each other are cut where either of them ends, so a boundary two shapes share is cut the
// same way from both sides and its pieces can be told apart at their ends.
void AddCuts(const Segment& segment, const Segment& other, std::vector<float>& cuts) {
    const Point along{segment.b.x - segment.a.x, segment.b.y - segment.a.y};
    const Point otherAlong{other.b.x - other.a.x, other.b.y - other.a.y};
    const Point offset{other.a.x - segment.a.x, other.a.y - segment.a.y};
    const float lengthSquared = along.x * along.x + along.y * along.y;
    if (lengthSquared <= 0.0f) {
        return;
    }
    const float denominator = along.x * otherAlong.y - along.y * otherAlong.x;
    const float length = std::sqrt(lengthSquared);
    const float reach = std::max(1.0f, std::hypot(offset.x, offset.y));
    if (std::fabs(denominator) <= 1e-6f * length * std::max(1.0f, std::hypot(otherAlong.x, otherAlong.y))) {
        // The two run in the same direction. They meet only where they lie along each other, which
        // is where either one ends.
        if (std::fabs(offset.x * along.y - offset.y * along.x) > 1e-4f * length * reach) {
            return;
        }
        AddCut(cuts, ((other.a.x - segment.a.x) * along.x + (other.a.y - segment.a.y) * along.y) / lengthSquared);
        AddCut(cuts, ((other.b.x - segment.a.x) * along.x + (other.b.y - segment.a.y) * along.y) / lengthSquared);
        return;
    }
    const float t = (offset.x * otherAlong.y - offset.y * otherAlong.x) / denominator;
    const float u = (offset.x * along.y - offset.y * along.x) / denominator;
    if (t < -1e-4f || t > 1.0f + 1e-4f || u < -1e-4f || u > 1.0f + 1e-4f) {
        return;
    }
    AddCut(cuts, t);
}

// Every segment cut at every place another segment crosses it, so that no two pieces cross anywhere
// but at their own ends. A piece that nothing crosses is the segment it started as.
std::vector<Piece> SplitSegments(const std::vector<Segment>& segments) {
    std::vector<Piece> pieces;
    std::vector<float> cuts;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const Segment& segment = segments[index];
        cuts.clear();
        for (std::size_t other = 0; other < segments.size(); ++other) {
            if (other != index) {
                AddCuts(segment, segments[other], cuts);
            }
        }
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end(), [](float left, float right) { return std::fabs(left - right) < 1e-5f; }), cuts.end());
        float previous = 0.0f;
        for (float cut : cuts) {
            pieces.push_back(Piece{PointAt(segment, previous), PointAt(segment, cut)});
            previous = cut;
        }
        pieces.push_back(Piece{PointAt(segment, previous), segment.b});
    }
    return pieces;
}

// The piece that carries on from this end. Every end of a boundary has one way on, so this only has
// a choice to make where two boundaries meet at a point, and there it takes the straighter one.
std::size_t NextPiece(const std::vector<Piece>& pieces, const std::vector<bool>& used, Point end, Point cameFrom) {
    std::size_t best = pieces.size();
    float bestStraightness = 0.0f;
    const Point incoming{end.x - cameFrom.x, end.y - cameFrom.y};
    const float incomingLength = std::hypot(incoming.x, incoming.y);
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        if (used[index] || !SamePoint(pieces[index].a, end, JOIN_TOLERANCE)) {
            continue;
        }
        // A piece whose direction cannot be read is worth less than any piece whose direction can,
        // so it only stands when there is nothing to compare it against.
        const Point outgoing{pieces[index].b.x - pieces[index].a.x, pieces[index].b.y - pieces[index].a.y};
        const float outgoingLength = std::hypot(outgoing.x, outgoing.y);
        const float straightness = incomingLength > 0.0f && outgoingLength > 0.0f
                                       ? (incoming.x * outgoing.x + incoming.y * outgoing.y) / (incomingLength * outgoingLength)
                                       : -2.0f;
        if (best == pieces.size() || straightness > bestStraightness) {
            bestStraightness = straightness;
            best = index;
        }
    }
    return best;
}

// The boundary pieces walked end to end into the loops that close them.
std::vector<std::vector<Point>> AssembleLoops(const std::vector<Piece>& pieces) {
    std::vector<std::vector<Point>> loops;
    std::vector<bool> used(pieces.size(), false);
    for (std::size_t start = 0; start < pieces.size(); ++start) {
        if (used[start]) {
            continue;
        }
        used[start] = true;
        std::vector<Point> loop;
        loop.push_back(pieces[start].a);
        Point end = pieces[start].b;
        bool closed = false;
        for (std::size_t step = 0; step <= pieces.size(); ++step) {
            if (SamePoint(end, loop.front(), JOIN_TOLERANCE)) {
                closed = true;
                break;
            }
            const std::size_t next = NextPiece(pieces, used, end, loop.back());
            if (next == pieces.size()) {
                break;
            }
            used[next] = true;
            if (!SamePoint(pieces[next].a, loop.back())) {
                loop.push_back(pieces[next].a);
            }
            end = pieces[next].b;
        }
        if (closed && loop.size() >= 3) {
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

void AppendPoint(std::string& path, Point point) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.3f %.3f ", point.x, point.y);
    path += buffer;
}

std::string PathOf(const std::vector<std::vector<Point>>& loops) {
    std::string path;
    for (const std::vector<Point>& loop : loops) {
        if (loop.size() < 3) {
            continue;
        }
        path += "M ";
        AppendPoint(path, loop.front());
        for (std::size_t index = 1; index < loop.size(); ++index) {
            path += "L ";
            AppendPoint(path, loop[index]);
        }
        path += "Z ";
    }
    return path;
}

}  // namespace

BooleanOperation ParseBooleanOperation(std::string_view text) {
    std::string name;
    for (char value : text) {
        if (std::isspace(static_cast<unsigned char>(value)) == 0) {
            name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
        }
    }
    if (name == "subtract" || name == "subtraction" || name == "difference" || name == "minus") {
        return BooleanOperation::SUBTRACT;
    }
    if (name == "intersect" || name == "intersection") {
        return BooleanOperation::INTERSECT;
    }
    if (name == "exclude" || name == "xor" || name == "exclusion") {
        return BooleanOperation::EXCLUDE;
    }
    // Everything else is a union, which is the operation that keeps every shape as it was: a
    // name nothing answers to is better read as the one operation that adds nothing to the shape
    // than as one that throws part of it away.
    return BooleanOperation::UNION;
}

const char* BooleanOperationName(BooleanOperation operation) {
    switch (operation) {
    case BooleanOperation::UNION:
        return "union";
    case BooleanOperation::SUBTRACT:
        return "subtract";
    case BooleanOperation::INTERSECT:
        return "intersect";
    case BooleanOperation::EXCLUDE:
        return "exclude";
    }
    return "union";
}

bool OutlinesContain(const std::vector<PathContour>& outlines, Point point) {
    return ShapeCovers(ReadShape(outlines), point);
}

std::string CombineOutlines(const std::vector<std::vector<PathContour>>& outlines, BooleanOperation operation) {
    std::vector<std::vector<Segment>> shapes;
    std::vector<Segment> everySegment;
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    bool started = false;
    for (const std::vector<PathContour>& shape : outlines) {
        std::vector<Segment> segments = ReadShape(shape);
        if (segments.empty()) {
            continue;
        }
        for (const Segment& segment : segments) {
            everySegment.push_back(segment);
            const Point corners[2] = {segment.a, segment.b};
            for (const Point& corner : corners) {
                if (!started) {
                    left = right = corner.x;
                    top = bottom = corner.y;
                    started = true;
                    continue;
                }
                left = std::min(left, corner.x);
                right = std::max(right, corner.x);
                top = std::min(top, corner.y);
                bottom = std::max(bottom, corner.y);
            }
        }
        shapes.push_back(std::move(segments));
    }
    if (shapes.empty()) {
        return std::string();
    }
    const float extent = std::max(std::hypot(right - left, bottom - top), 1.0f);
    const float probe = std::max(MIN_PROBE, extent * PROBE_SCALE);

    // Every place a boundary can be said to be part of the answer is a place the shapes do not
    // agree across. The space on each side of each piece is read, and the piece is kept when one
    // side is covered and the other is not, which is what makes a boundary.
    std::vector<Piece> boundary;
    for (const Piece& piece : SplitSegments(everySegment)) {
        const Point along{piece.b.x - piece.a.x, piece.b.y - piece.a.y};
        const float length = std::hypot(along.x, along.y);
        if (length <= POINT_EPSILON) {
            continue;
        }
        const Point normal{along.y / length, -along.x / length};
        const Point middle{(piece.a.x + piece.b.x) * 0.5f, (piece.a.y + piece.b.y) * 0.5f};
        const float step = std::min(probe, length * MAX_PROBE_SHARE);
        const Point leftSide{middle.x + normal.x * step, middle.y + normal.y * step};
        const Point rightSide{middle.x - normal.x * step, middle.y - normal.y * step};
        const bool leftCovered = ResultCovers(shapes, operation, leftSide);
        const bool rightCovered = ResultCovers(shapes, operation, rightSide);
        if (leftCovered == rightCovered) {
            continue;
        }
        // A kept piece is written with the answer on the side its normal points to, so every piece
        // travels the same way round the answer, and the loops that bound a hole are left wound the
        // other way round on their own.
        boundary.push_back(leftCovered ? piece : Piece{piece.b, piece.a});
    }
    if (boundary.empty()) {
        return std::string();
    }
    return PathOf(AssembleLoops(boundary));
}

}  // namespace Runtime
