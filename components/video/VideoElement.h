#pragma once

#include "components/image/ImageElement.h"

#include <string>

namespace Runtime {

// A video is a picture that is played rather than shown. It is built on the image, because the box,
// the file, the fitting, the region, the look, the corners and the turn are the picture's: a video
// is a picture, and what it brings is the playing of it.
//
// An image is animated by the clock and nothing else. The frame it draws is a fact about how long it
// has been on screen, there is no state to pause and none to seek, and the picture it was given is
// the picture it will always be drawing. A video is played by the node: the frame it draws is the
// frame its own playhead stands in, and the playhead is the one thing a pause, a speed, a seek and a
// loop all move. That is why the two are two components rather than one with a flag on it, and it is
// the whole of what a video adds.
//
// What it can play is what the image subsystem can read, which is a picture that holds more than one
// frame together with the timing the file itself wrote. A GIF, an animated WebP and an APNG hold
// their frames whole and are drawn one at a time; an AVI of Motion JPEG or an IVF of whole VP8
// pictures is a file too big to hold, so a frame of it is read out of the file when the playhead
// stands in it. Both kinds arrive here as the same thing, so this component does not know which of
// them it was given. A file whose frames the runtime cannot produce lays out as the box it would
// have covered and is refused the way an unreadable picture is, and the reason is written down in
// `docs/ImageSupport.md` rather than guessed at here. Nothing in this component reaches out to the
// operating system, so a video plays the same way on every machine.
class VideoElement : public ImageElement {
public:
    VideoElement();

    NodeKind Kind() const override;
    void ApplyBound(const std::string& property, const std::string& value) override;
    // A played picture is the one element that asks for the next pass by itself: while it is running
    // there is always a frame to draw that was not drawn a moment ago.
    bool Tick(float seconds) override;
    // A press and a drag on the bar of a video are the two ways a person moves its playhead: a press
    // on the track stands the playhead where the pointer is, a drag carries it along, and a press on
    // the button or anywhere on the picture itself starts the picture or stops it.
    bool OnPointerDown(Point point) override;
    bool OnDrag(Point point) override;
    void Activate() override;
    // A picture answers the activation itself: a click on it starts it or stops it, so the click is
    // the player's rather than an activation handed on to whatever holds the picture.
    bool AnswersActivation() const override;
    bool WantsHandCursor() const override;

    // Whether the picture is running. A video that is not running is drawn at the moment its
    // playhead stands at, which is the frame it was stopped on.
    void SetPlaying(bool value);
    bool Playing() const { return playing; }
    // Whether the end of the picture comes round to its beginning or stops it there.
    void SetLoop(bool value);
    bool Looping() const { return looping; }
    // How fast the playhead runs: one is the timing the file wrote, a half is half speed. A rate is
    // a positive number, so a rate that is not one is kept to the range a person can watch.
    void SetSpeed(float value);
    float Speed() const { return speed; }
    // Where in the picture the playhead stands, in seconds, and how long one pass through it lasts.
    // The seek is kept inside the picture, so a time past the end stands at the end.
    void SetTime(float seconds);
    void Seek(double seconds);
    float Position() const;
    float Duration() const;
    // How far through the picture the playhead stands, as a part of the whole. Nought for a picture
    // with no length to it, which is a picture that cannot be played.
    float Progress() const;
    // Whether the bar with the button and the progress on it is drawn at the foot of the box. A
    // video is played by a click whether or not its bar is drawn, the way every player is.
    void SetControls(bool value);
    bool ControlsShown() const { return controls; }

protected:
    void OnPaint(Renderer& renderer) override;
    // The frame the playhead stands in, which is what makes a pause, a speed and a seek mean
    // anything at all.
    int FrameAtMoment() const override;
    const char* MissingLabel() const override;

private:
    // Which part of its own chrome a press landed in, so lifting the pointer does the one thing that
    // part does rather than the thing a press on the picture does.
    enum class ControlPart {
        NONE,
        BUTTON,
        TRACK,
    };

    // Whether there is more than one frame to play, which is what tells a video from a picture.
    bool Playable() const;
    // The bar of the controls and the track drawn along it, in the pixels of the box. Both are worked
    // out in one place, so the track that is drawn and the track a press lands on are the same track.
    bool ControlBarRect(Rect& bar) const;
    bool ControlTrackRect(Rect& track) const;
    bool ControlButtonRect(Rect& button) const;
    // Where a press landed in the chrome, and where the playhead moves to for a press at a point.
    ControlPart PartAt(Point point) const;
    void SeekToPoint(Point point);
    // The bar, the button and the progress, drawn over the picture the box was drawn with.
    void DrawControls(Renderer& renderer);

    bool playing = true;
    bool looping = true;
    float speed = 1.0f;
    // Where the playhead stands, in seconds of the picture rather than of the clock. It is a double
    // because a pass is measured in tenths of a second and a playhead that is added to sixty times a
    // second would otherwise lose the small part of every step it takes.
    double playhead = 0.0;
    bool controls = false;
    // Which part of the chrome the pointer is holding from the press that is still down, so a drag
    // that began on the track scrubs and one that began on the picture does not.
    ControlPart heldPart = ControlPart::NONE;
};

}  // namespace Runtime
