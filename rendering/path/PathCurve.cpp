#include "rendering/path/PathCurve.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace Runtime {
namespace {

constexpr int CURVE_STEPS = 16;

struct Pen {
    float x = 0.0f;
    float y = 0.0f;
    float startX = 0.0f;
    float startY = 0.0f;
    float controlX = 0.0f;
    float controlY = 0.0f;
    char previous = 0;
};

float ReadNumber(std::string_view data, std::size_t& index) {
    while (index < data.size() && (data[index] == ' ' || data[index] == ',' || data[index] == '\n' || data[index] == '\r' || data[index] == '\t')) {
        ++index;
    }
    const std::size_t start = index;
    if (index < data.size() && (data[index] == '-' || data[index] == '+')) {
        ++index;
    }
    bool digits = false;
    if (index < data.size() && data[index] == '.') {
        ++index;
        while (index < data.size() && std::isdigit(static_cast<unsigned char>(data[index]))) {
            digits = true;
            ++index;
        }
    } else {
        while (index < data.size() && std::isdigit(static_cast<unsigned char>(data[index]))) {
            digits = true;
            ++index;
        }
        if (index < data.size() && data[index] == '.') {
            ++index;
            while (index < data.size() && std::isdigit(static_cast<unsigned char>(data[index]))) {
                digits = true;
                ++index;
            }
        }
    }
    if (digits && index < data.size() && (data[index] == 'e' || data[index] == 'E')) {
        const std::size_t mark = index++;
        if (index < data.size() && (data[index] == '+' || data[index] == '-')) {
            ++index;
        }
        bool exponent = false;
        while (index < data.size() && std::isdigit(static_cast<unsigned char>(data[index]))) {
            exponent = true;
            ++index;
        }
        if (!exponent) {
            index = mark;
        }
    }
    if (!digits) {
        index = start;
        return 0.0f;
    }
    return std::strtof(std::string(data.substr(start, index - start)).c_str(), nullptr);
}

void Add(std::vector<PathSample>& points, float x, float y) {
    if (!points.empty() && points.back().x == x && points.back().y == y) {
        return;
    }
    points.push_back(PathSample{x, y});
}

void Cubic(std::vector<PathSample>& points, Pen& pen, float x1, float y1, float x2, float y2, float x, float y) {
    for (int step = 1; step <= CURVE_STEPS; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(CURVE_STEPS);
        const float inverse = 1.0f - t;
        const float a = inverse * inverse * inverse;
        const float b = 3.0f * inverse * inverse * t;
        const float c = 3.0f * inverse * t * t;
        const float d = t * t * t;
        Add(points, a * pen.x + b * x1 + c * x2 + d * x, a * pen.y + b * y1 + c * y2 + d * y);
    }
    pen.controlX = x2;
    pen.controlY = y2;
    pen.x = x;
    pen.y = y;
    pen.previous = 'C';
}

void Quadratic(std::vector<PathSample>& points, Pen& pen, float x1, float y1, float x, float y) {
    for (int step = 1; step <= CURVE_STEPS; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(CURVE_STEPS);
        const float inverse = 1.0f - t;
        Add(points, inverse * inverse * pen.x + 2.0f * inverse * t * x1 + t * t * x,
            inverse * inverse * pen.y + 2.0f * inverse * t * y1 + t * t * y);
    }
    pen.controlX = x1;
    pen.controlY = y1;
    pen.x = x;
    pen.y = y;
    pen.previous = 'Q';
}

void Arc(std::vector<PathSample>& points, Pen& pen, float radiusX, float radiusY, float rotation, bool large, bool sweep, float endX, float endY) {
    if (radiusX == 0.0f || radiusY == 0.0f) {
        Add(points, endX, endY);
        pen.x = endX;
        pen.y = endY;
        pen.previous = 'A';
        return;
    }
    const float phi = rotation * 3.14159265f / 180.0f;
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    const float dx = (pen.x - endX) * 0.5f;
    const float dy = (pen.y - endY) * 0.5f;
    const float x1 = cosPhi * dx + sinPhi * dy;
    const float y1 = -sinPhi * dx + cosPhi * dy;
    float rx = std::fabs(radiusX);
    float ry = std::fabs(radiusY);
    const float lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1.0f) {
        const float scale = std::sqrt(lambda);
        rx *= scale;
        ry *= scale;
    }
    const float sign = (large == sweep) ? -1.0f : 1.0f;
    const float numerator = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const float denominator = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    const float root = denominator == 0.0f ? 0.0f : std::sqrt(std::max(0.0f, numerator / denominator));
    const float cx = sign * root * (rx * y1 / ry);
    const float cy = sign * root * -(ry * x1 / rx);
    const float centerX = cosPhi * cx - sinPhi * cy + (pen.x + endX) * 0.5f;
    const float centerY = sinPhi * cx + cosPhi * cy + (pen.y + endY) * 0.5f;
    const float start = std::atan2((y1 - cy) / ry, (x1 - cx) / rx);
    float delta = std::atan2((-y1 - cy) / ry, (-x1 - cx) / rx) - start;
    if (!sweep && delta > 0.0f) {
        delta -= 6.2831853f;
    } else if (sweep && delta < 0.0f) {
        delta += 6.2831853f;
    }
    const int arcSteps = std::max(CURVE_STEPS, static_cast<int>(std::ceil(std::fabs(delta) / (3.14159265f / 24.0f))));
    for (int step = 1; step <= arcSteps; ++step) {
        const float angle = start + delta * (static_cast<float>(step) / static_cast<float>(arcSteps));
        const float localX = rx * std::cos(angle);
        const float localY = ry * std::sin(angle);
        Add(points, centerX + cosPhi * localX - sinPhi * localY, centerY + sinPhi * localX + cosPhi * localY);
    }
    pen.x = endX;
    pen.y = endY;
    pen.previous = 'A';
}

}  // namespace

bool StartsNumber(std::string_view data, std::size_t index) {
    while (index < data.size() && (data[index] == ' ' || data[index] == ',' || data[index] == '\n' || data[index] == '\r' || data[index] == '\t')) {
        ++index;
    }
    if (index >= data.size()) {
        return false;
    }
    const char character = data[index];
    return character == '+' || character == '-' || character == '.' || (character >= '0' && character <= '9');
}

std::vector<PathContour> SampleContours(std::string_view data) {
    std::vector<PathContour> contours;
    std::vector<PathSample> points;
    Pen pen;
    std::size_t index = 0;
    char command = 0;
    auto finish = [&](bool closed) {
        if (points.size() > 1) {
            contours.push_back(PathContour{std::move(points), closed});
        }
        points.clear();
    };
    while (index < data.size()) {
        while (index < data.size() && (data[index] == ' ' || data[index] == ',' || data[index] == '\n' || data[index] == '\r' || data[index] == '\t')) {
            ++index;
        }
        if (index >= data.size()) {
            break;
        }
        if ((data[index] >= 'A' && data[index] <= 'Z') || (data[index] >= 'a' && data[index] <= 'z')) {
            command = data[index++];
        } else if (command == 0 || !StartsNumber(data, index)) {
            break;
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char absolute = relative ? static_cast<char>(command - 'a' + 'A') : command;
        if (absolute == 'Z') {
            Add(points, pen.startX, pen.startY);
            pen.x = pen.startX;
            pen.y = pen.startY;
            pen.previous = 'Z';
            finish(true);
            continue;
        }
        if (!StartsNumber(data, index)) {
            break;
        }
        const float originX = relative ? pen.x : 0.0f;
        const float originY = relative ? pen.y : 0.0f;
        if (absolute == 'M') {
            finish(false);
            pen.x = originX + ReadNumber(data, index);
            pen.y = originY + ReadNumber(data, index);
            pen.startX = pen.x;
            pen.startY = pen.y;
            Add(points, pen.x, pen.y);
            pen.previous = 'M';
            command = relative ? 'l' : 'L';
        } else if (absolute == 'L') {
            pen.x = originX + ReadNumber(data, index);
            pen.y = originY + ReadNumber(data, index);
            Add(points, pen.x, pen.y);
            pen.previous = 'L';
        } else if (absolute == 'H') {
            pen.x = originX + ReadNumber(data, index);
            Add(points, pen.x, pen.y);
            pen.previous = 'L';
        } else if (absolute == 'V') {
            pen.y = originY + ReadNumber(data, index);
            Add(points, pen.x, pen.y);
            pen.previous = 'L';
        } else if (absolute == 'C') {
            const float x1 = originX + ReadNumber(data, index);
            const float y1 = originY + ReadNumber(data, index);
            const float x2 = originX + ReadNumber(data, index);
            const float y2 = originY + ReadNumber(data, index);
            const float x = originX + ReadNumber(data, index);
            const float y = originY + ReadNumber(data, index);
            Cubic(points, pen, x1, y1, x2, y2, x, y);
        } else if (absolute == 'S') {
            const float x2 = originX + ReadNumber(data, index);
            const float y2 = originY + ReadNumber(data, index);
            const float x = originX + ReadNumber(data, index);
            const float y = originY + ReadNumber(data, index);
            const float x1 = pen.previous == 'C' ? pen.x * 2.0f - pen.controlX : pen.x;
            const float y1 = pen.previous == 'C' ? pen.y * 2.0f - pen.controlY : pen.y;
            Cubic(points, pen, x1, y1, x2, y2, x, y);
        } else if (absolute == 'Q') {
            const float x1 = originX + ReadNumber(data, index);
            const float y1 = originY + ReadNumber(data, index);
            const float x = originX + ReadNumber(data, index);
            const float y = originY + ReadNumber(data, index);
            Quadratic(points, pen, x1, y1, x, y);
        } else if (absolute == 'T') {
            const float x = originX + ReadNumber(data, index);
            const float y = originY + ReadNumber(data, index);
            const float x1 = pen.previous == 'Q' ? pen.x * 2.0f - pen.controlX : pen.x;
            const float y1 = pen.previous == 'Q' ? pen.y * 2.0f - pen.controlY : pen.y;
            Quadratic(points, pen, x1, y1, x, y);
        } else if (absolute == 'A') {
            const float rx = ReadNumber(data, index);
            const float ry = ReadNumber(data, index);
            const float rotation = ReadNumber(data, index);
            const float large = ReadNumber(data, index);
            const float sweep = ReadNumber(data, index);
            const float x = originX + ReadNumber(data, index);
            const float y = originY + ReadNumber(data, index);
            Arc(points, pen, rx, ry, rotation, large > 0.5f, sweep > 0.5f, x, y);
        }
    }
    finish(false);
    return contours;
}

std::vector<PathSample> SamplePath(std::string_view data) {
    std::vector<PathSample> points;
    for (const PathContour& contour : SampleContours(data)) {
        points.insert(points.end(), contour.points.begin(), contour.points.end());
    }
    return points;
}

}  // namespace Runtime
