#include "components/video/VideoElement.h"

#include "compiler/types/ValueParser.h"
#include "rendering/renderer/Renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace Runtime {
namespace {

// The bar a played picture is given at its foot: how tall it is, how far it stands from the edges of
// the box, and the least box it is worth drawing in. A bar drawn in a box too small to hold it is
// chrome laid over the picture rather than a control on it, so a small box is left to the picture.
constexpr float CONTROL_BAR_HEIGHT = 24.0f;
// How far the bar stands from the edges of the box, how far the button stands from the edge of the
// bar, the room left between the button and the track and before the end of the bar, the size of the
// button, and the thickness of the track.
constexpr float CONTROL_INSET = 8.0f;
constexpr float CONTROL_GLYPH_INSET = 6.0f;
constexpr float CONTROL_GAP = 8.0f;
constexpr float CONTROL_TRACK_HEIGHT = 4.0f;
constexpr float CONTROL_GLYPH_SIZE = CONTROL_BAR_HEIGHT - 8.0f;
constexpr float CONTROL_LEAST_WIDTH = 64.0f;
constexpr float CONTROL_LEAST_HEIGHT = CONTROL_BAR_HEIGHT + CONTROL_INSET * 2.0f;
// The bar is dark enough to be read over any picture, and the ink on it is the lightest thing in the
// box, because a control is read against whatever it is drawn over.
const Color CONTROL_BAR_FILL{0.0f, 0.0f, 0.0f, 0.55f};
const Color CONTROL_TRACK_FILL{1.0f, 1.0f, 1.0f, 0.28f};
const Color CONTROL_INK{1.0f, 1.0f, 1.0f, 0.95f};
// The shape a player starts with, written in a box sixteen units across however large it is drawn:
// a triangle pointing the way the picture will run. The shape a player stops with is two bars, which
// is a pair of rectangles and needs no path at all.
constexpr const char* PLAY_GLYPH = "M 3 1.5 L 13.5 8 L 3 14.5 Z";
constexpr float GLYPH_BOX = 16.0f;
// The widest rate the playhead is allowed to run at. It is far past any player's own list, and it is
// here so that a document writing a number rather than a rate still gets something watchable.
constexpr float SPEED_LIMIT = 16.0f;

}  // namespace

VideoElement::VideoElement() {
    // A video is a picture, so it is measured as the picture it holds rather than as a box: the
    // image underneath has already started it at the size of the file it will play.
}

NodeKind VideoElement::Kind() const {
    return NodeKind::VIDEO;
}

void VideoElement::SetPlaying(bool value) {
    if (value && !playing) {
        const float span = Duration();
        if (span > 0.0f && Position() >= span) {
            // A picture that is started again from its end plays again rather than standing still,
            // which is the one thing a play button at the end of a video does anywhere.
            playhead = 0.0;
        }
    }
    playing = value;
}

void VideoElement::SetLoop(bool value) {
    looping = value;
}

void VideoElement::SetSpeed(float value) {
    speed = std::clamp(value, 0.0f, SPEED_LIMIT);
}

void VideoElement::SetTime(float seconds) {
    Seek(static_cast<double>(seconds));
}

void VideoElement::Seek(double seconds) {
    const double span = static_cast<double>(Duration());
    playhead = std::clamp(seconds, 0.0, span > 0.0 ? span : 0.0);
}

float VideoElement::Position() const {
    return static_cast<float>(playhead);
}

float VideoElement::Duration() const {
    // A pass is a pass only when there is more than one frame in it. A picture of a single frame has
    // nothing to run through, so it is no length at all: it is a picture rather than a video, and a
    // playhead measured against nothing would be a playhead with nowhere to stand.
    if (PictureFrameCount() <= 1) {
        return 0.0f;
    }
    return static_cast<float>(PictureDurationMilliseconds()) / 1000.0f;
}

float VideoElement::Progress() const {
    const double span = static_cast<double>(Duration());
    if (span <= 0.0) {
        return 0.0f;
    }
    return static_cast<float>(playhead / span);
}

void VideoElement::SetControls(bool value) {
    controls = value;
}

bool VideoElement::Playable() const {
    // A picture with one frame is a picture, however it is drawn: there is nothing to play, and no
    // frame after this one for a pause, a speed or a loop to have any meaning about.
    return PictureFrameCount() > 1;
}

bool VideoElement::WantsHandCursor() const {
    // A video is something a pointer acts on, because a click anywhere on the picture starts it or
    // stops it. It says so over a picture that can be played whether or not its bar is drawn, and
    // over one that cannot be played it says what its own cursor says.
    return RectangleElement::WantsHandCursor() || Playable();
}

bool VideoElement::Tick(float seconds) {
    bool changed = Element::Tick(seconds);
    if (!playing || speed <= 0.0f || !Playable()) {
        return changed;
    }
    const double span = static_cast<double>(Duration());
    if (span <= 0.0) {
        return changed;
    }
    playhead += static_cast<double>(seconds) * static_cast<double>(speed);
    if (playhead >= span) {
        // A pass that has run out either comes round to its beginning or stops where it ended, which
        // is the whole of what the loop of it decides.
        if (looping) {
            playhead = std::fmod(playhead, span);
        } else {
            playhead = span;
            playing = false;
        }
    }
    // While the picture is running there is always a frame to draw that was not drawn a moment ago,
    // so the pass that drew this one is never the last one.
    return true;
}

bool VideoElement::AnswersActivation() const { return true; }

void VideoElement::Activate() {
    // A click on a video is still the click a document hears, and what it does to the picture is
    // what a player does with it: the button starts it or stops it, anywhere on the picture does the
    // same, and a click that landed on the track has moved the playhead already and does nothing
    // more.
    ImageElement::Activate();
    const ControlPart part = heldPart;
    heldPart = ControlPart::NONE;
    if (part != ControlPart::TRACK) {
        SetPlaying(!playing);
    }
}

void VideoElement::ApplyBound(const std::string& property, const std::string& value) {
    // The picture answers for everything it owns first, so a binding aimed at the file, the fitting,
    // the region, the look, or the box still lands on the video underneath it.
    ImageElement::ApplyBound(property, value);
    // A bound value the language does not read as a boolean leaves the video as it was, which is the
    // same reading every other bound value that cannot be read gets.
    const std::optional<bool> flag = ParseBool(value);
    if (property == "playing") {
        if (flag.has_value()) {
            SetPlaying(*flag);
        }
    } else if (property == "loop") {
        if (flag.has_value()) {
            SetLoop(*flag);
        }
    } else if (property == "controls") {
        if (flag.has_value()) {
            SetControls(*flag);
        }
    } else if (property == "speed") {
        SetSpeed(std::strtof(value.c_str(), nullptr));
    } else if (property == "time") {
        SetTime(std::strtof(value.c_str(), nullptr));
    }
}

int VideoElement::FrameAtMoment() const {
    const unsigned long long span = PictureDurationMilliseconds();
    const double stood = playhead < 0.0 ? 0.0 : playhead;
    const unsigned long long at = static_cast<unsigned long long>(std::llround(stood * 1000.0));
    // A playhead standing at the end of the picture is inside its last frame, which is the frame a
    // video that stopped holds; anything else is the frame that moment falls in.
    if (span > 0 && at >= span) {
        return FrameForMilliseconds(span - 1);
    }
    return FrameForMilliseconds(at);
}

const char* VideoElement::MissingLabel() const {
    // A video whose file could not be read is named after what it would have been, which is the one
    // thing a box holding no picture still knows about itself.
    return "Video";
}

void VideoElement::OnPaint(Renderer& renderer) {
    // A video is drawn as the picture it plays, and every other thing drawn about it is the box the
    // picture was drawn in.
    ImageElement::OnPaint(renderer);
    // A bar over a picture that cannot be played would be a control for something that is not there,
    // so the bar belongs to a picture with more than one frame to run through.
    if (controls && Playable()) {
        DrawControls(renderer);
    }
}

bool VideoElement::ControlBarRect(Rect& bar) const {
    const Rect area = Bounds();
    if (area.width < CONTROL_LEAST_WIDTH || area.height < CONTROL_LEAST_HEIGHT) {
        // A bar drawn in a box too small to hold it is chrome laid over the picture rather than a
        // control on it, so a box that small has no bar to draw and none to press.
        return false;
    }
    bar = Rect{area.x + CONTROL_INSET, area.y + area.height - CONTROL_INSET - CONTROL_BAR_HEIGHT,
               area.width - CONTROL_INSET * 2.0f, CONTROL_BAR_HEIGHT};
    return true;
}

bool VideoElement::ControlButtonRect(Rect& button) const {
    Rect bar;
    if (!ControlBarRect(bar)) {
        return false;
    }
    button = Rect{bar.x + CONTROL_GLYPH_INSET, bar.y + (CONTROL_BAR_HEIGHT - CONTROL_GLYPH_SIZE) * 0.5f,
                  CONTROL_GLYPH_SIZE, CONTROL_GLYPH_SIZE};
    return true;
}

bool VideoElement::ControlTrackRect(Rect& track) const {
    Rect bar;
    Rect button;
    if (!ControlBarRect(bar) || !ControlButtonRect(button)) {
        return false;
    }
    const float start = button.x + button.width + CONTROL_GAP;
    const float wide = bar.x + bar.width - CONTROL_GAP - start;
    if (wide <= 8.0f) {
        // A track with no length in it is not a track: there is nowhere along it to stand, and
        // nothing for a press to mean.
        return false;
    }
    track = Rect{start, bar.y + (CONTROL_BAR_HEIGHT - CONTROL_TRACK_HEIGHT) * 0.5f, wide, CONTROL_TRACK_HEIGHT};
    return true;
}

VideoElement::ControlPart VideoElement::PartAt(Point point) const {
    if (!controls || !Playable()) {
        return ControlPart::NONE;
    }
    Rect button;
    if (ControlButtonRect(button) && ContainsPoint(button, point)) {
        return ControlPart::BUTTON;
    }
    // The track is pressed along the height of the bar and not only along the four pixels of it, so
    // a press that means to move the playhead is not a press that missed it.
    Rect track;
    if (ControlTrackRect(track)) {
        const Rect reach{track.x, track.y - CONTROL_INSET, track.width, track.height + CONTROL_INSET * 2.0f};
        if (ContainsPoint(reach, point)) {
            return ControlPart::TRACK;
        }
    }
    return ControlPart::NONE;
}

void VideoElement::SeekToPoint(Point point) {
    Rect track;
    if (!ControlTrackRect(track)) {
        return;
    }
    // The place along the track the pointer stands at is the place in the picture it is asking for,
    // and a pointer past either end of the track asks for that end rather than for nothing.
    const float part = std::clamp((point.x - track.x) / track.width, 0.0f, 1.0f);
    Seek(static_cast<double>(part) * static_cast<double>(Duration()));
}

bool VideoElement::OnPointerDown(Point point) {
    const bool pressed = ImageElement::OnPointerDown(point);
    heldPart = PartAt(point);
    if (heldPart == ControlPart::TRACK) {
        // The playhead stands where the pointer is as soon as the pointer is down, which is the
        // whole of what a person is asking for by pressing a track at all.
        SeekToPoint(point);
    }
    return pressed;
}

bool VideoElement::OnDrag(Point point) {
    if (heldPart != ControlPart::TRACK) {
        // A drag that did not begin on the track belongs to whatever is underneath the video.
        return false;
    }
    SeekToPoint(point);
    return true;
}

void VideoElement::DrawControls(Renderer& renderer) {
    Rect bar;
    if (!ControlBarRect(bar)) {
        return;
    }
    // The bar is a rounded box, which is the same box every other rounded rectangle in the runtime
    // is drawn as, so the chrome of a player is made of the components the document also draws with.
    renderer.FillRectangle(bar, CONTROL_BAR_FILL, CONTROL_BAR_HEIGHT * 0.5f);

    Rect button;
    if (ControlButtonRect(button)) {
        if (playing) {
            // Two bars beside each other, which is the shape a picture is stopped with.
            const float barWidth = button.width * 0.3f;
            renderer.FillRectangle(Rect{button.x, button.y, barWidth, button.height}, CONTROL_INK, 1.0f);
            renderer.FillRectangle(Rect{button.x + button.width - barWidth, button.y, barWidth, button.height}, CONTROL_INK, 1.0f);
        } else {
            // A triangle pointing the way the picture will run, drawn as the path a shape is drawn as.
            VectorPaint paint;
            paint.bounds = button;
            paint.viewX = 0.0f;
            paint.viewY = 0.0f;
            paint.viewWidth = GLYPH_BOX;
            paint.viewHeight = GLYPH_BOX;
            paint.fill = CONTROL_INK;
            paint.fillEnabled = true;
            paint.data = PLAY_GLYPH;
            renderer.DrawVector(paint);
        }
    }

    // How far through the picture the playhead stands, drawn beside the button: a track the length
    // of a pass, with the part already played laid over it.
    Rect track;
    if (!ControlTrackRect(track)) {
        return;
    }
    renderer.FillRectangle(track, CONTROL_TRACK_FILL, CONTROL_TRACK_HEIGHT * 0.5f);
    const float played = track.width * std::clamp(Progress(), 0.0f, 1.0f);
    if (played > 0.0f) {
        renderer.FillRectangle(Rect{track.x, track.y, played, track.height}, CONTROL_INK, CONTROL_TRACK_HEIGHT * 0.5f);
    }
}

}  // namespace Runtime
