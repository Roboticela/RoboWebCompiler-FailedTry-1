#include "rendering/image/ImageDecoder.h"

#include "rendering/path/PathCurve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Runtime {
namespace {

constexpr int SAMPLE_LIMIT = 1024;
constexpr float PI = 3.14159265f;

struct Mat {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float e = 0.0f;
    float f = 0.0f;
};

struct Paint {
    bool none = true;
    unsigned char red = 0;
    unsigned char green = 0;
    unsigned char blue = 0;
    unsigned char alpha = 255;
};

struct Style {
    Mat matrix;
    Paint fill;
    Paint stroke;
    float strokeWidth = 1.0f;
    float opacity = 1.0f;
    float fillOpacity = 1.0f;
    float strokeOpacity = 1.0f;
    float miterLimit = 4.0f;
    bool evenOdd = false;
    char lineCap = 'b';
    char lineJoin = 'm';
};

struct SamplePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct Canvas {
    int width = 0;
    int height = 0;
    int sample = 1;
    std::vector<unsigned char> mask;
    std::vector<unsigned char> bgra;
};

Mat Multiply(const Mat& left, const Mat& right) {
    Mat result;
    result.a = left.a * right.a + left.c * right.b;
    result.b = left.b * right.a + left.d * right.b;
    result.c = left.a * right.c + left.c * right.d;
    result.d = left.b * right.c + left.d * right.d;
    result.e = left.a * right.e + left.c * right.f + left.e;
    result.f = left.b * right.e + left.d * right.f + left.f;
    return result;
}

Mat Translate(float x, float y) {
    Mat matrix;
    matrix.e = x;
    matrix.f = y;
    return matrix;
}

Mat Scale(float x, float y) {
    Mat matrix;
    matrix.a = x;
    matrix.d = y;
    return matrix;
}

Mat Rotate(float degrees) {
    const float radians = degrees * PI / 180.0f;
    Mat matrix;
    matrix.a = std::cos(radians);
    matrix.b = std::sin(radians);
    matrix.c = -matrix.b;
    matrix.d = matrix.a;
    return matrix;
}

SamplePoint TransformPoint(const Mat& matrix, float x, float y, int sample) {
    SamplePoint point;
    point.x = (matrix.a * x + matrix.c * y + matrix.e) * static_cast<float>(sample);
    point.y = (matrix.b * x + matrix.d * y + matrix.f) * static_cast<float>(sample);
    return point;
}

bool SkipSpace(std::string_view text, std::size_t& index) {
    while (index < text.size() && (text[index] == ' ' || text[index] == '\n' || text[index] == '\r' || text[index] == '\t' || text[index] == ',')) {
        ++index;
    }
    return index < text.size();
}

float ReadFloat(std::string_view text, std::size_t& index, bool& ok) {
    if (!SkipSpace(text, index)) {
        ok = false;
        return 0.0f;
    }
    const char* begin = text.data() + index;
    char* end = nullptr;
    const float value = std::strtof(begin, &end);
    if (end == begin) {
        ok = false;
        return 0.0f;
    }
    index += static_cast<std::size_t>(end - begin);
    while (index < text.size() && ((text[index] >= 'a' && text[index] <= 'z') || (text[index] >= 'A' && text[index] <= 'Z') || text[index] == '%')) {
        ++index;
    }
    ok = true;
    return value;
}

int HexValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\n' || text.front() == '\r' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    return text;
}

bool Equals(std::string_view text, std::string_view literal) {
    if (text.size() != literal.size()) {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        char left = text[index];
        char right = literal[index];
        if (left >= 'A' && left <= 'Z') {
            left = static_cast<char>(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = static_cast<char>(right - 'A' + 'a');
        }
        if (left != right) {
            return false;
        }
    }
    return true;
}

bool ParseColor(std::string_view text, const SvgDecodeOptions& options, Paint& paint) {
    text = Trim(text);
    if (text.empty()) {
        return false;
    }
    if (Equals(text, "none") || Equals(text, "transparent")) {
        paint.none = true;
        return true;
    }
    paint.none = false;
    paint.alpha = 255;
    if (Equals(text, "currentcolor")) {
        paint.red = options.currentRed;
        paint.green = options.currentGreen;
        paint.blue = options.currentBlue;
        return true;
    }
    if (Equals(text, "black")) {
        paint.red = 0;
        paint.green = 0;
        paint.blue = 0;
        return true;
    }
    if (Equals(text, "white")) {
        paint.red = 255;
        paint.green = 255;
        paint.blue = 255;
        return true;
    }
    if (Equals(text, "red")) {
        paint.red = 255;
        paint.green = 0;
        paint.blue = 0;
        return true;
    }
    if (Equals(text, "green")) {
        paint.red = 0;
        paint.green = 128;
        paint.blue = 0;
        return true;
    }
    if (Equals(text, "blue")) {
        paint.red = 0;
        paint.green = 0;
        paint.blue = 255;
        return true;
    }
    if (Equals(text, "gray") || Equals(text, "grey")) {
        paint.red = 128;
        paint.green = 128;
        paint.blue = 128;
        return true;
    }
    if (text.front() == '#') {
        const std::string_view hex = text.substr(1);
        auto pair = [&](std::size_t index) { return HexValue(hex[index]) * 16 + HexValue(hex[index + 1]); };
        if (hex.size() == 3 || hex.size() == 4) {
            const int red = HexValue(hex[0]);
            const int green = HexValue(hex[1]);
            const int blue = HexValue(hex[2]);
            if (red < 0 || green < 0 || blue < 0) {
                return false;
            }
            paint.red = static_cast<unsigned char>(red * 17);
            paint.green = static_cast<unsigned char>(green * 17);
            paint.blue = static_cast<unsigned char>(blue * 17);
            if (hex.size() == 4) {
                const int alpha = HexValue(hex[3]);
                if (alpha < 0) {
                    return false;
                }
                paint.alpha = static_cast<unsigned char>(alpha * 17);
            }
            return true;
        }
        if (hex.size() == 6 || hex.size() == 8) {
            const int red = pair(0);
            const int green = pair(2);
            const int blue = pair(4);
            if (red < 0 || green < 0 || blue < 0) {
                return false;
            }
            paint.red = static_cast<unsigned char>(red);
            paint.green = static_cast<unsigned char>(green);
            paint.blue = static_cast<unsigned char>(blue);
            if (hex.size() == 8) {
                const int alpha = pair(6);
                if (alpha < 0) {
                    return false;
                }
                paint.alpha = static_cast<unsigned char>(alpha);
            }
            return true;
        }
        return false;
    }
    if (text.size() > 4 && (text[0] == 'r' || text[0] == 'R') && (text[1] == 'g' || text[1] == 'G') && (text[2] == 'b' || text[2] == 'B')) {
        std::size_t index = text.find('(');
        if (index == std::string_view::npos) {
            return false;
        }
        ++index;
        bool ok = true;
        float red = ReadFloat(text, index, ok);
        float green = ReadFloat(text, index, ok);
        float blue = ReadFloat(text, index, ok);
        if (!ok) {
            return false;
        }
        if (text.find('%') != std::string_view::npos) {
            red *= 2.55f;
            green *= 2.55f;
            blue *= 2.55f;
        }
        paint.red = static_cast<unsigned char>(std::clamp(red, 0.0f, 255.0f));
        paint.green = static_cast<unsigned char>(std::clamp(green, 0.0f, 255.0f));
        paint.blue = static_cast<unsigned char>(std::clamp(blue, 0.0f, 255.0f));
        return true;
    }
    return false;
}

std::string_view Attribute(std::string_view tag, std::string_view name) {
    for (std::size_t index = 0; index + name.size() + 2 < tag.size(); ++index) {
        if (index > 0 && tag[index - 1] != ' ' && tag[index - 1] != '\n' && tag[index - 1] != '\t' && tag[index - 1] != '\r') {
            continue;
        }
        if (tag.compare(index, name.size(), name) != 0) {
            continue;
        }
        std::size_t cursor = index + name.size();
        while (cursor < tag.size() && (tag[cursor] == ' ' || tag[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor >= tag.size() || tag[cursor] != '=') {
            continue;
        }
        ++cursor;
        while (cursor < tag.size() && (tag[cursor] == ' ' || tag[cursor] == '\t')) {
            ++cursor;
        }
        if (cursor >= tag.size()) {
            return {};
        }
        const char quote = tag[cursor];
        if (quote != '"' && quote != '\'') {
            const std::size_t start = cursor;
            while (cursor < tag.size() && tag[cursor] != ' ' && tag[cursor] != '\t' && tag[cursor] != '/' && tag[cursor] != '>') {
                ++cursor;
            }
            return tag.substr(start, cursor - start);
        }
        ++cursor;
        const std::size_t start = cursor;
        while (cursor < tag.size() && tag[cursor] != quote) {
            ++cursor;
        }
        return tag.substr(start, cursor - start);
    }
    return {};
}

float AttributeFloat(std::string_view tag, std::string_view name, float fallback) {
    const std::string_view value = Attribute(tag, name);
    if (value.empty()) {
        return fallback;
    }
    std::size_t index = 0;
    bool ok = false;
    const float number = ReadFloat(value, index, ok);
    return ok ? number : fallback;
}

void ApplyPaint(Style& style, std::string_view name, std::string_view value, const SvgDecodeOptions& options) {
    Paint color;
    if (Equals(name, "fill")) {
        if (ParseColor(value, options, color)) {
            style.fill = color;
        }
    } else if (Equals(name, "stroke")) {
        if (ParseColor(value, options, color)) {
            style.stroke = color;
        }
    } else if (Equals(name, "stroke-width")) {
        std::size_t index = 0;
        bool ok = false;
        const float width = ReadFloat(value, index, ok);
        if (ok) {
            style.strokeWidth = width;
        }
    } else if (Equals(name, "opacity")) {
        std::size_t index = 0;
        bool ok = false;
        const float opacity = ReadFloat(value, index, ok);
        if (ok) {
            style.opacity *= std::clamp(opacity, 0.0f, 1.0f);
        }
    } else if (Equals(name, "fill-opacity")) {
        std::size_t index = 0;
        bool ok = false;
        const float opacity = ReadFloat(value, index, ok);
        if (ok) {
            style.fillOpacity = std::clamp(opacity, 0.0f, 1.0f);
        }
    } else if (Equals(name, "stroke-opacity")) {
        std::size_t index = 0;
        bool ok = false;
        const float opacity = ReadFloat(value, index, ok);
        if (ok) {
            style.strokeOpacity = std::clamp(opacity, 0.0f, 1.0f);
        }
    } else if (Equals(name, "fill-rule")) {
        style.evenOdd = Equals(Trim(value), "evenodd");
    } else if (Equals(name, "stroke-linecap")) {
        const std::string_view cap = Trim(value);
        style.lineCap = Equals(cap, "round") ? 'r' : Equals(cap, "square") ? 's' : 'b';
    } else if (Equals(name, "stroke-linejoin")) {
        const std::string_view join = Trim(value);
        style.lineJoin = Equals(join, "round") ? 'r' : Equals(join, "bevel") ? 'b' : 'm';
    } else if (Equals(name, "stroke-miterlimit")) {
        std::size_t index = 0;
        bool ok = false;
        const float limit = ReadFloat(value, index, ok);
        if (ok && limit > 0.0f) {
            style.miterLimit = limit;
        }
    }
}

void ApplyStyleText(Style& style, std::string_view text, const SvgDecodeOptions& options) {
    std::size_t index = 0;
    while (index < text.size()) {
        const std::size_t split = text.find(';', index);
        const std::string_view item = text.substr(index, split == std::string_view::npos ? text.size() - index : split - index);
        const std::size_t colon = item.find(':');
        if (colon != std::string_view::npos) {
            ApplyPaint(style, Trim(item.substr(0, colon)), Trim(item.substr(colon + 1)), options);
        }
        if (split == std::string_view::npos) {
            break;
        }
        index = split + 1;
    }
}

void ApplyAttributes(Style& style, std::string_view tag, const SvgDecodeOptions& options) {
    const char* names[] = {"fill", "stroke", "stroke-width", "opacity", "fill-opacity", "stroke-opacity", "fill-rule", "stroke-linecap", "stroke-linejoin", "stroke-miterlimit"};
    for (const char* name : names) {
        const std::string_view value = Attribute(tag, name);
        if (!value.empty()) {
            ApplyPaint(style, name, value, options);
        }
    }
    const std::string_view inlineStyle = Attribute(tag, "style");
    if (!inlineStyle.empty()) {
        ApplyStyleText(style, inlineStyle, options);
    }
}

Mat ParseTransform(std::string_view text) {
    Mat matrix;
    std::size_t index = 0;
    while (SkipSpace(text, index)) {
        if (index >= text.size() || !((text[index] >= 'a' && text[index] <= 'z') || (text[index] >= 'A' && text[index] <= 'Z'))) {
            break;
        }
        const std::size_t nameAt = index;
        while (index < text.size() && ((text[index] >= 'a' && text[index] <= 'z') || (text[index] >= 'A' && text[index] <= 'Z'))) {
            ++index;
        }
        const std::string_view name = text.substr(nameAt, index - nameAt);
        if (!SkipSpace(text, index) || text[index] != '(') {
            break;
        }
        ++index;
        bool ok = true;
        float values[6] = {};
        int count = 0;
        while (count < 6 && SkipSpace(text, index) && text[index] != ')') {
            values[count++] = ReadFloat(text, index, ok);
            if (!ok) {
                break;
            }
        }
        if (index < text.size() && text[index] == ')') {
            ++index;
        }
        Mat local;
        if (Equals(name, "matrix") && count >= 6) {
            local.a = values[0];
            local.b = values[1];
            local.c = values[2];
            local.d = values[3];
            local.e = values[4];
            local.f = values[5];
        } else if (Equals(name, "translate")) {
            local = Translate(values[0], count > 1 ? values[1] : 0.0f);
        } else if (Equals(name, "scale")) {
            local = Scale(values[0], count > 1 ? values[1] : values[0]);
        } else if (Equals(name, "rotate") && count >= 1) {
            if (count >= 3) {
                local = Multiply(Translate(values[1], values[2]), Multiply(Rotate(values[0]), Translate(-values[1], -values[2])));
            } else {
                local = Rotate(values[0]);
            }
        } else if (Equals(name, "skewx")) {
            local.c = std::tan(values[0] * PI / 180.0f);
        } else if (Equals(name, "skewy")) {
            local.b = std::tan(values[0] * PI / 180.0f);
        }
        matrix = Multiply(matrix, local);
    }
    return matrix;
}

void ApplyTransform(Style& style, std::string_view tag) {
    const std::string_view transform = Attribute(tag, "transform");
    if (!transform.empty()) {
        style.matrix = Multiply(style.matrix, ParseTransform(transform));
    }
}

float AxisScale(const Mat& matrix) {
    return (std::hypot(matrix.a, matrix.b) + std::hypot(matrix.c, matrix.d)) * 0.5f;
}

void Blend(Canvas& canvas, const Paint& paint, float opacity) {
    if (paint.none || opacity <= 0.0f || paint.alpha == 0) {
        return;
    }
    const int sampleCount = canvas.sample * canvas.sample;
    const float sourceAlpha = opacity * (static_cast<float>(paint.alpha) / 255.0f);
    const int sampleWidth = canvas.width * canvas.sample;
    for (int y = 0; y < canvas.height; ++y) {
        for (int x = 0; x < canvas.width; ++x) {
            int covered = 0;
            for (int sy = 0; sy < canvas.sample; ++sy) {
                const int row = (y * canvas.sample + sy) * sampleWidth + x * canvas.sample;
                for (int sx = 0; sx < canvas.sample; ++sx) {
                    covered += canvas.mask[static_cast<std::size_t>(row + sx)] ? 1 : 0;
                }
            }
            if (covered == 0) {
                continue;
            }
            const float coverage = static_cast<float>(covered) / static_cast<float>(sampleCount);
            const float alpha = sourceAlpha * coverage;
            const std::size_t pixel = static_cast<std::size_t>((y * canvas.width + x) * 4);
            const float keep = 1.0f - alpha;
            canvas.bgra[pixel] = static_cast<unsigned char>(std::clamp(paint.blue * alpha + canvas.bgra[pixel] * keep, 0.0f, 255.0f));
            canvas.bgra[pixel + 1] = static_cast<unsigned char>(std::clamp(paint.green * alpha + canvas.bgra[pixel + 1] * keep, 0.0f, 255.0f));
            canvas.bgra[pixel + 2] = static_cast<unsigned char>(std::clamp(paint.red * alpha + canvas.bgra[pixel + 2] * keep, 0.0f, 255.0f));
            canvas.bgra[pixel + 3] = static_cast<unsigned char>(std::clamp(255.0f * alpha + canvas.bgra[pixel + 3] * keep, 0.0f, 255.0f));
        }
    }
}

void FillMask(Canvas& canvas, const std::vector<std::vector<SamplePoint>>& contours, bool evenOdd, bool closeOpen, bool unite) {
    const int width = canvas.width * canvas.sample;
    const int height = canvas.height * canvas.sample;
    if (!unite) {
        std::fill(canvas.mask.begin(), canvas.mask.end(), static_cast<unsigned char>(0));
    }
    struct Hit {
        float x;
        int direction;
    };
    std::vector<Hit> hits;
    for (int row = 0; row < height; ++row) {
        hits.clear();
        const float y = static_cast<float>(row) + 0.5f;
        for (const std::vector<SamplePoint>& contour : contours) {
            if (contour.size() < 2) {
                continue;
            }
            const bool alreadyClosed = std::fabs(contour.front().x - contour.back().x) < 0.01f && std::fabs(contour.front().y - contour.back().y) < 0.01f;
            const int count = alreadyClosed ? static_cast<int>(contour.size()) - 1 : static_cast<int>(contour.size());
            const int edges = (alreadyClosed || closeOpen) ? count : count - 1;
            for (int edge = 0; edge < edges; ++edge) {
                const SamplePoint& from = contour[static_cast<std::size_t>(edge)];
                const SamplePoint& to = contour[static_cast<std::size_t>((edge + 1) % count)];
                if ((from.y <= y && y < to.y) || (to.y <= y && y < from.y)) {
                    const float span = to.y - from.y;
                    if (span == 0.0f) {
                        continue;
                    }
                    Hit hit;
                    hit.x = from.x + (y - from.y) * (to.x - from.x) / span;
                    hit.direction = to.y > from.y ? 1 : -1;
                    hits.push_back(hit);
                }
            }
        }
        if (hits.empty()) {
            continue;
        }
        std::sort(hits.begin(), hits.end(), [](const Hit& left, const Hit& right) { return left.x < right.x; });
        int winding = 0;
        for (std::size_t index = 0; index < hits.size(); ++index) {
            const int nextWinding = winding + hits[index].direction;
            if (index > 0) {
                const bool inside = evenOdd ? (winding & 1) != 0 : winding != 0;
                if (inside) {
                    int x0 = static_cast<int>(std::floor(hits[index - 1].x));
                    int x1 = static_cast<int>(std::ceil(hits[index].x));
                    x0 = std::max(0, x0);
                    x1 = std::min(width, x1);
                    unsigned char* rowPixels = canvas.mask.data() + static_cast<std::size_t>(row * width);
                    for (int x = x0; x < x1; ++x) {
                        rowPixels[x] = 1;
                    }
                }
            }
            winding = nextWinding;
        }
    }
}

void AddCircle(std::vector<std::vector<SamplePoint>>& outlines, float x, float y, float radius) {
    if (radius <= 0.2f) {
        return;
    }
    constexpr int STEPS = 16;
    std::vector<SamplePoint> contour;
    contour.reserve(static_cast<std::size_t>(STEPS + 1));
    for (int step = 0; step <= STEPS; ++step) {
        const float angle = PI * 2.0f * static_cast<float>(step) / static_cast<float>(STEPS);
        contour.push_back(SamplePoint{x + std::cos(angle) * radius, y + std::sin(angle) * radius});
    }
    outlines.push_back(std::move(contour));
}

void AddQuad(std::vector<std::vector<SamplePoint>>& outlines, SamplePoint from, SamplePoint to, float half, bool squareStart, bool squareEnd) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);
    if (length < 0.01f || half <= 0.0f) {
        return;
    }
    const float nx = -dy / length * half;
    const float ny = dx / length * half;
    const float ux = dx / length * half;
    const float uy = dy / length * half;
    if (squareStart) {
        from.x -= ux;
        from.y -= uy;
    }
    if (squareEnd) {
        to.x += ux;
        to.y += uy;
    }
    outlines.push_back({
        {from.x + nx, from.y + ny},
        {to.x + nx, to.y + ny},
        {to.x - nx, to.y - ny},
        {from.x - nx, from.y - ny},
        {from.x + nx, from.y + ny},
    });
}

SamplePoint Normal(SamplePoint from, SamplePoint to, float half) {
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);
    if (length < 0.01f) {
        return {};
    }
    return {-dy / length * half, dx / length * half};
}

void AddJoin(std::vector<std::vector<SamplePoint>>& outlines, SamplePoint previous, SamplePoint point, SamplePoint next, float half, char join, float miterLimit) {
    const SamplePoint left = Normal(previous, point, half);
    const SamplePoint right = Normal(point, next, half);
    const float cross = (point.x - previous.x) * (next.y - point.y) - (point.y - previous.y) * (next.x - point.x);
    const float side = cross >= 0.0f ? 1.0f : -1.0f;
    const SamplePoint a{point.x + left.x * side, point.y + left.y * side};
    const SamplePoint b{point.x + right.x * side, point.y + right.y * side};
    if (join == 'r') {
        AddCircle(outlines, point.x, point.y, half);
        return;
    }
    if (join == 'm') {
        const float dx0 = point.x - previous.x;
        const float dy0 = point.y - previous.y;
        const float dx1 = next.x - point.x;
        const float dy1 = next.y - point.y;
        const float det = dx0 * dy1 - dy0 * dx1;
        if (std::fabs(det) > 0.001f) {
            const float t = ((b.x - a.x) * dy1 - (b.y - a.y) * dx1) / det;
            const SamplePoint miter{a.x + dx0 * t, a.y + dy0 * t};
            const float reach = std::hypot(miter.x - point.x, miter.y - point.y);
            if (reach <= miterLimit * half * 2.0f) {
                outlines.push_back({point, a, miter, b, point});
                return;
            }
        }
    }
    outlines.push_back({point, a, b, point});
}

void StrokeContour(std::vector<std::vector<SamplePoint>>& outlines, const std::vector<SamplePoint>& contour, bool closed, float half, char cap, char join, float miterLimit) {
    if (contour.size() < 2 || half <= 0.05f) {
        return;
    }
    std::vector<SamplePoint> points = contour;
    if (closed && (std::fabs(points.front().x - points.back().x) > 0.01f || std::fabs(points.front().y - points.back().y) > 0.01f)) {
        points.push_back(points.front());
    }
    const bool loop = closed && points.size() > 2;
    const int last = static_cast<int>(points.size()) - 1;
    for (int index = 0; index < last; ++index) {
        const bool squareStart = !loop && index == 0 && cap == 's';
        const bool squareEnd = !loop && index == last - 1 && cap == 's';
        AddQuad(outlines, points[static_cast<std::size_t>(index)], points[static_cast<std::size_t>(index + 1)], half, squareStart, squareEnd);
    }
    if (!loop && cap == 'r') {
        AddCircle(outlines, points.front().x, points.front().y, half);
        AddCircle(outlines, points.back().x, points.back().y, half);
    }
    const int joins = loop ? last : last - 1;
    for (int index = 0; index < joins; ++index) {
        const int current = loop ? index : index + 1;
        const int before = current == 0 ? last - 1 : current - 1;
        const int after = current + 1;
        AddJoin(outlines, points[static_cast<std::size_t>(before)], points[static_cast<std::size_t>(current)], points[static_cast<std::size_t>(after)], half, join, miterLimit);
    }
}

std::vector<SamplePoint> MapContour(const std::vector<PathSample>& source, const Mat& matrix, int sample) {
    std::vector<SamplePoint> points;
    points.reserve(source.size());
    for (const PathSample& point : source) {
        points.push_back(TransformPoint(matrix, point.x, point.y, sample));
    }
    return points;
}

void PaintContours(Canvas& canvas, const std::vector<std::vector<SamplePoint>>& contours, const std::vector<char>& closed, const Style& style) {
    if (contours.empty()) {
        return;
    }
    if (!style.fill.none && style.fillOpacity > 0.0f && style.opacity > 0.0f) {
        FillMask(canvas, contours, style.evenOdd, true, false);
        Blend(canvas, style.fill, style.opacity * style.fillOpacity);
    }
    if (style.stroke.none || style.strokeWidth <= 0.0f || style.strokeOpacity <= 0.0f || style.opacity <= 0.0f) {
        return;
    }
    const float half = style.strokeWidth * AxisScale(style.matrix) * static_cast<float>(canvas.sample) * 0.5f;
    if (half <= 0.05f) {
        return;
    }
    std::vector<std::vector<SamplePoint>> outlines;
    for (std::size_t index = 0; index < contours.size(); ++index) {
        const bool isClosed = index < closed.size() && closed[index] != 0;
        StrokeContour(outlines, contours[index], isClosed, half, style.lineCap, style.lineJoin, style.miterLimit);
    }
    if (outlines.empty()) {
        return;
    }
    std::fill(canvas.mask.begin(), canvas.mask.end(), static_cast<unsigned char>(0));
    for (const std::vector<SamplePoint>& outline : outlines) {
        FillMask(canvas, {outline}, false, true, true);
    }
    Blend(canvas, style.stroke, style.opacity * style.strokeOpacity);
}

void PaintPath(Canvas& canvas, std::string_view data, const Style& style) {
    const std::vector<PathContour> contours = SampleContours(data);
    std::vector<std::vector<SamplePoint>> mapped;
    std::vector<char> closed;
    mapped.reserve(contours.size());
    for (const PathContour& contour : contours) {
        if (contour.points.size() < 2) {
            continue;
        }
        mapped.push_back(MapContour(contour.points, style.matrix, canvas.sample));
        closed.push_back(contour.closed ? 1 : 0);
    }
    PaintContours(canvas, mapped, closed, style);
}

void AddEllipse(std::vector<SamplePoint>& points, float cx, float cy, float rx, float ry) {
    constexpr int STEPS = 48;
    points.reserve(static_cast<std::size_t>(STEPS + 1));
    for (int step = 0; step <= STEPS; ++step) {
        const float angle = PI * 2.0f * static_cast<float>(step) / static_cast<float>(STEPS);
        points.push_back({cx + std::cos(angle) * rx, cy + std::sin(angle) * ry});
    }
}

void PaintBasic(Canvas& canvas, const std::vector<SamplePoint>& points, bool closed, const Style& style) {
    PaintContours(canvas, {points}, {static_cast<char>(closed ? 1 : 0)}, style);
}

std::vector<SamplePoint> ReadPointList(std::string_view text, const Mat& matrix, int sample) {
    std::vector<SamplePoint> points;
    std::size_t index = 0;
    bool ok = true;
    while (ok && SkipSpace(text, index)) {
        const float x = ReadFloat(text, index, ok);
        const float y = ok ? ReadFloat(text, index, ok) : 0.0f;
        if (ok) {
            points.push_back(TransformPoint(matrix, x, y, sample));
        }
    }
    return points;
}

std::size_t TagEnd(std::string_view xml, std::size_t from) {
    bool quoted = false;
    char quote = 0;
    for (std::size_t index = from; index < xml.size(); ++index) {
        const char character = xml[index];
        if (quoted) {
            if (character == quote) {
                quoted = false;
            }
            continue;
        }
        if (character == '"' || character == '\'') {
            quoted = true;
            quote = character;
            continue;
        }
        if (character == '>') {
            return index;
        }
    }
    return std::string_view::npos;
}

void SkipElement(std::string_view xml, std::size_t& index, std::string_view name) {
    int depth = 1;
    while (index < xml.size() && depth > 0) {
        const std::size_t found = xml.find('<', index);
        if (found == std::string_view::npos) {
            index = xml.size();
            return;
        }
        const std::size_t end = TagEnd(xml, found);
        if (end == std::string_view::npos) {
            index = xml.size();
            return;
        }
        std::string_view token = xml.substr(found + 1, end - found - 1);
        const bool close = !token.empty() && token.front() == '/';
        if (close) {
            token.remove_prefix(1);
        }
        while (!token.empty() && (token.front() == ' ' || token.front() == '\n' || token.front() == '\t')) {
            token.remove_prefix(1);
        }
        std::size_t length = 0;
        while (length < token.size() && ((token[length] >= 'a' && token[length] <= 'z') || (token[length] >= 'A' && token[length] <= 'Z') || token[length] == ':' || token[length] == '_')) {
            ++length;
        }
        const std::string_view tag = token.substr(0, length);
        const bool self = !close && token.find('/') != std::string_view::npos;
        if (Equals(tag, name)) {
            if (close) {
                --depth;
            } else if (!self) {
                ++depth;
            }
        }
        index = end + 1;
    }
}

void PaintElement(std::string_view xml, std::size_t& index, Style style, Canvas& canvas, const SvgDecodeOptions& options) {
    while (index < xml.size()) {
        const std::size_t found = xml.find('<', index);
        if (found == std::string_view::npos) {
            index = xml.size();
            return;
        }
        if (found + 4 < xml.size() && xml.compare(found, 4, "<!--") == 0) {
            const std::size_t end = xml.find("-->", found + 4);
            index = end == std::string_view::npos ? xml.size() : end + 3;
            continue;
        }
        const std::size_t end = TagEnd(xml, found);
        if (end == std::string_view::npos) {
            index = xml.size();
            return;
        }
        std::string_view token = xml.substr(found + 1, end - found - 1);
        if (token.empty() || token.front() == '?' || token.front() == '!') {
            index = end + 1;
            continue;
        }
        const bool closing = token.front() == '/';
        if (closing) {
            index = found;
            return;
        }
        std::size_t nameLength = 0;
        while (nameLength < token.size() && ((token[nameLength] >= 'a' && token[nameLength] <= 'z') || (token[nameLength] >= 'A' && token[nameLength] <= 'Z') || token[nameLength] == ':' || token[nameLength] == '_' || token[nameLength] == '-')) {
            ++nameLength;
        }
        std::string_view name = token.substr(0, nameLength);
        const std::size_t colon = name.rfind(':');
        if (colon != std::string_view::npos) {
            name.remove_prefix(colon + 1);
        }
        const bool self = !token.empty() && token.back() == '/';
        index = end + 1;
        if (Equals(name, "defs") || Equals(name, "title") || Equals(name, "desc") || Equals(name, "metadata") || Equals(name, "style") || Equals(name, "clippath") ||
            Equals(name, "mask") || Equals(name, "lineargradient") || Equals(name, "radialgradient") || Equals(name, "pattern") || Equals(name, "filter") ||
            Equals(name, "text") || Equals(name, "symbol")) {
            if (!self) {
                SkipElement(xml, index, name);
            }
            continue;
        }
        Style child = style;
        ApplyAttributes(child, token, options);
        ApplyTransform(child, token);
        if (Equals(name, "path")) {
            PaintPath(canvas, Attribute(token, "d"), child);
        } else if (Equals(name, "rect")) {
            const float x = AttributeFloat(token, "x", 0.0f);
            const float y = AttributeFloat(token, "y", 0.0f);
            const float width = AttributeFloat(token, "width", 0.0f);
            const float height = AttributeFloat(token, "height", 0.0f);
            float rx = AttributeFloat(token, "rx", -1.0f);
            float ry = AttributeFloat(token, "ry", -1.0f);
            if (width > 0.0f && height > 0.0f) {
                if (rx < 0.0f) {
                    rx = ry > 0.0f ? ry : 0.0f;
                }
                if (ry < 0.0f) {
                    ry = rx;
                }
                rx = std::min(rx, width * 0.5f);
                ry = std::min(ry, height * 0.5f);
                std::vector<SamplePoint> points;
                if (rx <= 0.0f || ry <= 0.0f) {
                    points.push_back(TransformPoint(child.matrix, x, y, canvas.sample));
                    points.push_back(TransformPoint(child.matrix, x + width, y, canvas.sample));
                    points.push_back(TransformPoint(child.matrix, x + width, y + height, canvas.sample));
                    points.push_back(TransformPoint(child.matrix, x, y + height, canvas.sample));
                    points.push_back(points.front());
                } else {
                    constexpr int CORNER = 6;
                    auto corner = [&](float cx, float cy, float start) {
                        for (int step = 0; step <= CORNER; ++step) {
                            const float angle = start + (PI * 0.5f) * static_cast<float>(step) / static_cast<float>(CORNER);
                            points.push_back(TransformPoint(child.matrix, cx + std::cos(angle) * rx, cy + std::sin(angle) * ry, canvas.sample));
                        }
                    };
                    corner(x + width - rx, y + ry, -PI * 0.5f);
                    corner(x + width - rx, y + height - ry, 0.0f);
                    corner(x + rx, y + height - ry, PI * 0.5f);
                    corner(x + rx, y + ry, PI);
                    points.push_back(points.front());
                }
                PaintBasic(canvas, points, true, child);
            }
        } else if (Equals(name, "circle") || Equals(name, "ellipse")) {
            const float cx = AttributeFloat(token, "cx", 0.0f);
            const float cy = AttributeFloat(token, "cy", 0.0f);
            const float rx = Equals(name, "circle") ? AttributeFloat(token, "r", 0.0f) : AttributeFloat(token, "rx", 0.0f);
            const float ry = Equals(name, "circle") ? rx : AttributeFloat(token, "ry", 0.0f);
            if (rx > 0.0f && ry > 0.0f) {
                std::vector<SamplePoint> local;
                AddEllipse(local, cx, cy, rx, ry);
                std::vector<SamplePoint> points;
                points.reserve(local.size());
                for (const SamplePoint& point : local) {
                    points.push_back(TransformPoint(child.matrix, point.x, point.y, canvas.sample));
                }
                PaintBasic(canvas, points, true, child);
            }
        } else if (Equals(name, "line")) {
            std::vector<SamplePoint> points = {
                TransformPoint(child.matrix, AttributeFloat(token, "x1", 0.0f), AttributeFloat(token, "y1", 0.0f), canvas.sample),
                TransformPoint(child.matrix, AttributeFloat(token, "x2", 0.0f), AttributeFloat(token, "y2", 0.0f), canvas.sample),
            };
            child.fill.none = true;
            PaintBasic(canvas, points, false, child);
        } else if (Equals(name, "polyline") || Equals(name, "polygon")) {
            std::vector<SamplePoint> points = ReadPointList(Attribute(token, "points"), child.matrix, canvas.sample);
            if (points.size() >= 2) {
                PaintBasic(canvas, points, Equals(name, "polygon"), child);
            }
        } else if (Equals(name, "g") || Equals(name, "a") || Equals(name, "svg") || Equals(name, "switch")) {
            if (!self) {
                const std::size_t body = index;
                PaintElement(xml, index, child, canvas, options);
                if (index < xml.size() && xml[index] == '<') {
                    const std::size_t closeEnd = TagEnd(xml, index);
                    if (closeEnd != std::string_view::npos) {
                        index = closeEnd + 1;
                    }
                }
                if (index == body) {
                    SkipElement(xml, index, name);
                }
            }
        } else if (!self) {
            SkipElement(xml, index, name);
        }
    }
}

bool ReadPixel(std::string_view text, float& value) {
    text = Trim(text);
    if (text.empty() || text.find('%') != std::string_view::npos || text.find("em") != std::string_view::npos || text.find("ex") != std::string_view::npos) {
        return false;
    }
    std::size_t index = 0;
    bool ok = false;
    value = ReadFloat(text, index, ok);
    return ok && value > 0.0f;
}

std::string FormatNumber(float value) {
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
    std::string text = buffer;
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

void AppendAttribute(std::string& xml, std::string_view name, std::string_view value) {
    xml.push_back(' ');
    xml.append(name);
    xml.append("=\"");
    for (const char character : value) {
        if (character == '&') {
            xml.append("&amp;");
        } else if (character == '"') {
            xml.append("&quot;");
        } else if (character == '<') {
            xml.append("&lt;");
        } else {
            xml.push_back(character);
        }
    }
    xml.push_back('"');
}

std::string FormatPaint(const Paint& paint, float opacity) {
    const int alpha = std::clamp(static_cast<int>(std::lround(static_cast<float>(paint.alpha) * opacity)), 0, 255);
    if (alpha <= 0) {
        return "transparent";
    }
    char buffer[16];
    if (alpha >= 255) {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", paint.red, paint.green, paint.blue);
    } else {
        std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", paint.red, paint.green, paint.blue, alpha);
    }
    return buffer;
}

bool IsIdentity(const Mat& matrix) {
    return std::fabs(matrix.a - 1.0f) < 0.0001f && std::fabs(matrix.b) < 0.0001f && std::fabs(matrix.c) < 0.0001f && std::fabs(matrix.d - 1.0f) < 0.0001f &&
           std::fabs(matrix.e) < 0.0001f && std::fabs(matrix.f) < 0.0001f;
}

std::string TransformedPath(std::string_view data, const Mat& matrix) {
    if (IsIdentity(matrix)) {
        return std::string(data);
    }
    std::string path;
    for (const PathContour& contour : SampleContours(data)) {
        if (contour.points.empty()) {
            continue;
        }
        bool first = true;
        for (const PathSample& point : contour.points) {
            const float x = matrix.a * point.x + matrix.c * point.y + matrix.e;
            const float y = matrix.b * point.x + matrix.d * point.y + matrix.f;
            path += first ? "M" : "L";
            path += FormatNumber(x);
            path.push_back(' ');
            path += FormatNumber(y);
            first = false;
        }
        if (contour.closed) {
            path.push_back('Z');
        }
    }
    return path;
}

void EmitShape(std::string& xml, std::string data, const Style& style, const std::string& viewBox, float boxWidth, float boxHeight, int& serial) {
    data = TransformedPath(data, style.matrix);
    if (data.empty()) {
        return;
    }
    const bool fill = !style.fill.none && style.fillOpacity > 0.0f && style.opacity > 0.0f;
    const bool stroke = !style.stroke.none && style.strokeWidth > 0.0f && style.strokeOpacity > 0.0f && style.opacity > 0.0f;
    if (!fill && !stroke) {
        return;
    }
    xml.append("<path");
    AppendAttribute(xml, "name", "Path " + std::to_string(++serial));
    AppendAttribute(xml, "d", data);
    AppendAttribute(xml, "x", "0");
    AppendAttribute(xml, "y", "0");
    AppendAttribute(xml, "width", FormatNumber(boxWidth));
    AppendAttribute(xml, "height", FormatNumber(boxHeight));
    AppendAttribute(xml, "viewBox", viewBox);
    if (fill) {
        AppendAttribute(xml, "fill", FormatPaint(style.fill, style.opacity * style.fillOpacity));
    }
    if (stroke) {
        AppendAttribute(xml, "stroke", FormatPaint(style.stroke, style.opacity * style.strokeOpacity));
        AppendAttribute(xml, "strokeWidth", FormatNumber(style.strokeWidth));
        if (style.lineCap == 'r') {
            AppendAttribute(xml, "lineCap", "round");
        } else if (style.lineCap == 's') {
            AppendAttribute(xml, "lineCap", "square");
        }
        if (style.lineJoin == 'r') {
            AppendAttribute(xml, "lineJoin", "round");
        } else if (style.lineJoin == 'b') {
            AppendAttribute(xml, "lineJoin", "bevel");
        }
    }
    if (style.evenOdd) {
        AppendAttribute(xml, "fillRule", "evenodd");
    }
    xml.append("/>");
}

std::string RectPath(float x, float y, float width, float height, float rx, float ry) {
    if (rx <= 0.0f || ry <= 0.0f) {
        return "M" + FormatNumber(x) + " " + FormatNumber(y) + "H" + FormatNumber(x + width) + "V" + FormatNumber(y + height) + "H" + FormatNumber(x) + "Z";
    }
    return "M" + FormatNumber(x + rx) + " " + FormatNumber(y) + "H" + FormatNumber(x + width - rx) + "A" + FormatNumber(rx) + " " + FormatNumber(ry) + " 0 0 1 " +
           FormatNumber(x + width) + " " + FormatNumber(y + ry) + "V" + FormatNumber(y + height - ry) + "A" + FormatNumber(rx) + " " + FormatNumber(ry) + " 0 0 1 " +
           FormatNumber(x + width - rx) + " " + FormatNumber(y + height) + "H" + FormatNumber(x + rx) + "A" + FormatNumber(rx) + " " + FormatNumber(ry) + " 0 0 1 " +
           FormatNumber(x) + " " + FormatNumber(y + height - ry) + "V" + FormatNumber(y + ry) + "A" + FormatNumber(rx) + " " + FormatNumber(ry) + " 0 0 1 " +
           FormatNumber(x + rx) + " " + FormatNumber(y) + "Z";
}

std::string EllipsePath(float cx, float cy, float rx, float ry) {
    return "M" + FormatNumber(cx - rx) + " " + FormatNumber(cy) + "A" + FormatNumber(rx) + " " + FormatNumber(ry) + " 0 1 1 " + FormatNumber(cx + rx) + " " +
           FormatNumber(cy) + "A" + FormatNumber(rx) + " " + FormatNumber(ry) + " 0 1 1 " + FormatNumber(cx - rx) + " " + FormatNumber(cy) + "Z";
}

std::string PointsPath(std::string_view text, bool close) {
    std::string path;
    std::size_t index = 0;
    bool ok = true;
    bool first = true;
    while (ok && SkipSpace(text, index)) {
        const float x = ReadFloat(text, index, ok);
        const float y = ok ? ReadFloat(text, index, ok) : 0.0f;
        if (!ok) {
            break;
        }
        path += first ? "M" : "L";
        path += FormatNumber(x);
        path.push_back(' ');
        path += FormatNumber(y);
        first = false;
    }
    if (close && !path.empty()) {
        path.push_back('Z');
    }
    return path;
}

void EmitLayers(std::string_view xml, std::size_t& index, Style style, std::string& out, const std::string& viewBox, float boxWidth, float boxHeight, int& serial, const SvgDecodeOptions& options) {
    while (index < xml.size()) {
        const std::size_t found = xml.find('<', index);
        if (found == std::string_view::npos) {
            index = xml.size();
            return;
        }
        if (found + 4 < xml.size() && xml.compare(found, 4, "<!--") == 0) {
            const std::size_t end = xml.find("-->", found + 4);
            index = end == std::string_view::npos ? xml.size() : end + 3;
            continue;
        }
        const std::size_t end = TagEnd(xml, found);
        if (end == std::string_view::npos) {
            index = xml.size();
            return;
        }
        std::string_view token = xml.substr(found + 1, end - found - 1);
        if (token.empty() || token.front() == '?' || token.front() == '!') {
            index = end + 1;
            continue;
        }
        if (token.front() == '/') {
            index = found;
            return;
        }
        std::size_t nameLength = 0;
        while (nameLength < token.size() && ((token[nameLength] >= 'a' && token[nameLength] <= 'z') || (token[nameLength] >= 'A' && token[nameLength] <= 'Z') || token[nameLength] == ':' || token[nameLength] == '_' || token[nameLength] == '-')) {
            ++nameLength;
        }
        std::string_view name = token.substr(0, nameLength);
        const std::size_t colon = name.rfind(':');
        if (colon != std::string_view::npos) {
            name.remove_prefix(colon + 1);
        }
        const bool self = !token.empty() && token.back() == '/';
        index = end + 1;
        if (Equals(name, "defs") || Equals(name, "title") || Equals(name, "desc") || Equals(name, "metadata") || Equals(name, "style") || Equals(name, "clippath") ||
            Equals(name, "mask") || Equals(name, "lineargradient") || Equals(name, "radialgradient") || Equals(name, "pattern") || Equals(name, "filter") ||
            Equals(name, "text") || Equals(name, "symbol")) {
            if (!self) {
                SkipElement(xml, index, name);
            }
            continue;
        }
        Style child = style;
        ApplyAttributes(child, token, options);
        ApplyTransform(child, token);
        if (Equals(name, "path")) {
            EmitShape(out, std::string(Attribute(token, "d")), child, viewBox, boxWidth, boxHeight, serial);
        } else if (Equals(name, "rect")) {
            const float x = AttributeFloat(token, "x", 0.0f);
            const float y = AttributeFloat(token, "y", 0.0f);
            const float width = AttributeFloat(token, "width", 0.0f);
            const float height = AttributeFloat(token, "height", 0.0f);
            float rx = AttributeFloat(token, "rx", -1.0f);
            float ry = AttributeFloat(token, "ry", -1.0f);
            if (width > 0.0f && height > 0.0f) {
                if (rx < 0.0f) {
                    rx = ry > 0.0f ? ry : 0.0f;
                }
                if (ry < 0.0f) {
                    ry = rx;
                }
                EmitShape(out, RectPath(x, y, width, height, std::min(rx, width * 0.5f), std::min(ry, height * 0.5f)), child, viewBox, boxWidth, boxHeight, serial);
            }
        } else if (Equals(name, "circle") || Equals(name, "ellipse")) {
            const float cx = AttributeFloat(token, "cx", 0.0f);
            const float cy = AttributeFloat(token, "cy", 0.0f);
            const float rx = Equals(name, "circle") ? AttributeFloat(token, "r", 0.0f) : AttributeFloat(token, "rx", 0.0f);
            const float ry = Equals(name, "circle") ? rx : AttributeFloat(token, "ry", 0.0f);
            if (rx > 0.0f && ry > 0.0f) {
                EmitShape(out, EllipsePath(cx, cy, rx, ry), child, viewBox, boxWidth, boxHeight, serial);
            }
        } else if (Equals(name, "line")) {
            child.fill.none = true;
            EmitShape(out, "M" + FormatNumber(AttributeFloat(token, "x1", 0.0f)) + " " + FormatNumber(AttributeFloat(token, "y1", 0.0f)) + "L" + FormatNumber(AttributeFloat(token, "x2", 0.0f)) + " " + FormatNumber(AttributeFloat(token, "y2", 0.0f)), child, viewBox, boxWidth, boxHeight, serial);
        } else if (Equals(name, "polyline") || Equals(name, "polygon")) {
            EmitShape(out, PointsPath(Attribute(token, "points"), Equals(name, "polygon")), child, viewBox, boxWidth, boxHeight, serial);
        } else if (Equals(name, "g") || Equals(name, "a") || Equals(name, "svg") || Equals(name, "switch")) {
            if (!self) {
                std::string inner;
                const std::size_t body = index;
                EmitLayers(xml, index, child, inner, viewBox, boxWidth, boxHeight, serial, options);
                if (index < xml.size() && xml[index] == '<') {
                    const std::size_t closeEnd = TagEnd(xml, index);
                    if (closeEnd != std::string_view::npos) {
                        index = closeEnd + 1;
                    }
                }
                if (index == body) {
                    SkipElement(xml, index, name);
                }
                if (!inner.empty()) {
                    out.append("<group");
                    AppendAttribute(out, "name", "Group " + std::to_string(++serial));
                    out.push_back('>');
                    out += inner;
                    out.append("</group>");
                }
            }
        } else if (!self) {
            SkipElement(xml, index, name);
        }
    }
}

bool ReadViewBox(std::string_view text, float& minX, float& minY, float& width, float& height) {
    std::size_t index = 0;
    bool ok = true;
    minX = ReadFloat(text, index, ok);
    minY = ok ? ReadFloat(text, index, ok) : 0.0f;
    width = ok ? ReadFloat(text, index, ok) : 0.0f;
    height = ok ? ReadFloat(text, index, ok) : 0.0f;
    return ok && width > 0.0f && height > 0.0f;
}

}  // namespace

std::string VectorXmlFromSvg(std::string_view svg, std::string_view name, unsigned char red, unsigned char green, unsigned char blue) {
    const std::size_t svgAt = svg.find("<svg");
    if (svgAt == std::string_view::npos) {
        return {};
    }
    const std::size_t svgEnd = TagEnd(svg, svgAt);
    if (svgEnd == std::string_view::npos) {
        return {};
    }
    const std::string_view root = svg.substr(svgAt + 1, svgEnd - svgAt - 1);
    float viewX = 0.0f;
    float viewY = 0.0f;
    float viewWidth = 0.0f;
    float viewHeight = 0.0f;
    ReadPixel(Attribute(root, "width"), viewWidth);
    ReadPixel(Attribute(root, "height"), viewHeight);
    float boxWidth = viewWidth;
    float boxHeight = viewHeight;
    if (!Attribute(root, "viewBox").empty()) {
        ReadViewBox(Attribute(root, "viewBox"), viewX, viewY, boxWidth, boxHeight);
    }
    if (boxWidth <= 0.0f || boxHeight <= 0.0f) {
        boxWidth = viewWidth > 0.0f ? viewWidth : 24.0f;
        boxHeight = viewHeight > 0.0f ? viewHeight : 24.0f;
    }
    const std::string viewBox = FormatNumber(viewX) + " " + FormatNumber(viewY) + " " + FormatNumber(boxWidth) + " " + FormatNumber(boxHeight);
    SvgDecodeOptions options;
    options.currentRed = red;
    options.currentGreen = green;
    options.currentBlue = blue;
    Style style;
    style.fill.none = false;
    ApplyAttributes(style, root, options);
    ApplyTransform(style, root);
    std::string layers;
    int serial = 0;
    std::size_t index = svgEnd + 1;
    EmitLayers(svg, index, style, layers, viewBox, boxWidth, boxHeight, serial, options);
    if (layers.empty()) {
        return {};
    }
    std::string xml = "<vector";
    AppendAttribute(xml, "name", name.empty() ? std::string_view("Vector") : name);
    AppendAttribute(xml, "width", FormatNumber(boxWidth));
    AppendAttribute(xml, "height", FormatNumber(boxHeight));
    AppendAttribute(xml, "viewBox", viewBox);
    AppendAttribute(xml, "fill", "#18181A");
    xml.push_back('>');
    xml += layers;
    xml.append("</vector>");
    return xml;
}

bool DecodeSvgBytes(const unsigned char* data, int size, DecodedImage& image, const SvgDecodeOptions& options) {
    if (data == nullptr || size < 8) {
        return false;
    }
    const std::string_view xml(reinterpret_cast<const char*>(data), static_cast<std::size_t>(size));
    const std::size_t svgAt = xml.find("<svg");
    if (svgAt == std::string_view::npos) {
        return false;
    }
    const std::size_t svgEnd = TagEnd(xml, svgAt);
    if (svgEnd == std::string_view::npos) {
        return false;
    }
    const std::string_view root = xml.substr(svgAt + 1, svgEnd - svgAt - 1);
    float viewX = 0.0f;
    float viewY = 0.0f;
    float viewWidth = 0.0f;
    float viewHeight = 0.0f;
    ReadPixel(Attribute(root, "width"), viewWidth);
    ReadPixel(Attribute(root, "height"), viewHeight);
    const std::string_view viewBox = Attribute(root, "viewBox");
    float boxWidth = viewWidth;
    float boxHeight = viewHeight;
    if (!viewBox.empty()) {
        ReadViewBox(viewBox, viewX, viewY, boxWidth, boxHeight);
    }
    if (boxWidth <= 0.0f || boxHeight <= 0.0f) {
        boxWidth = viewWidth > 0.0f ? viewWidth : 256.0f;
        boxHeight = viewHeight > 0.0f ? viewHeight : 256.0f;
        viewX = 0.0f;
        viewY = 0.0f;
    }
    int outWidth = 0;
    int outHeight = 0;
    if (options.maxEdge > 0) {
        const float fit = static_cast<float>(std::clamp(options.maxEdge, 1, SAMPLE_LIMIT)) / std::max(boxWidth, boxHeight);
        outWidth = std::max(1, static_cast<int>(std::lround(boxWidth * fit)));
        outHeight = std::max(1, static_cast<int>(std::lround(boxHeight * fit)));
    } else {
        outWidth = viewWidth > 0.0f ? static_cast<int>(std::lround(viewWidth)) : static_cast<int>(std::lround(boxWidth));
        outHeight = viewHeight > 0.0f ? static_cast<int>(std::lround(viewHeight)) : static_cast<int>(std::lround(boxHeight));
        outWidth = std::clamp(outWidth, 1, SAMPLE_LIMIT);
        outHeight = std::clamp(outHeight, 1, SAMPLE_LIMIT);
    }
    const float scale = std::min(static_cast<float>(outWidth) / boxWidth, static_cast<float>(outHeight) / boxHeight);
    const float padX = (static_cast<float>(outWidth) - boxWidth * scale) * 0.5f;
    const float padY = (static_cast<float>(outHeight) - boxHeight * scale) * 0.5f;
    Canvas canvas;
    canvas.width = outWidth;
    canvas.height = outHeight;
    canvas.sample = 4;
    while (canvas.sample > 1 && (outWidth * canvas.sample > SAMPLE_LIMIT || outHeight * canvas.sample > SAMPLE_LIMIT)) {
        --canvas.sample;
    }
    canvas.mask.assign(static_cast<std::size_t>(outWidth * canvas.sample * outHeight * canvas.sample), 0);
    canvas.bgra.assign(static_cast<std::size_t>(outWidth * outHeight * 4), 0);
    Style style;
    style.fill.none = false;
    style.fill.red = 0;
    style.fill.green = 0;
    style.fill.blue = 0;
    style.matrix = Multiply(Translate(padX, padY), Multiply(Scale(scale, scale), Translate(-viewX, -viewY)));
    ApplyAttributes(style, root, options);
    ApplyTransform(style, root);
    std::size_t index = svgEnd + 1;
    PaintElement(xml, index, style, canvas, options);
    ImageFrame frame;
    frame.width = outWidth;
    frame.height = outHeight;
    // A canvas is painted by laying one shape over another, which is compositing done on
    // premultiplied pixels: a shape laid over what is already there is folded in with its alpha
    // rather than carried beside it, so the pixels that come out are already in that form.
    frame.premultiplied = true;
    frame.bgra = std::move(canvas.bgra);
    image.format = "svg";
    image.frames.clear();
    image.frames.push_back(std::move(frame));
    return true;
}

}  // namespace Runtime
