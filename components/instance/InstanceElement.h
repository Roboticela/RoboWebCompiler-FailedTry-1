#pragma once

#include "components/frame/Frame.h"

#include <string>

namespace Runtime {

// An instance is a placed copy of a component. It is a frame in every way a document cares about,
// so a copy lays its content out exactly as its own box asks it to. What makes it a copy rather than
// a box is the name of the definition it was placed from and, when that definition belongs to a set
// of variants, the variant this copy was told to show.
//
// A copy holds the content of its master, resolved when the document is built, so a copy that was
// told to show something different for one of its master's children holds the difference instead of
// the original. The difference is written down as an override, which is what makes an instance more
// than a frame that happens to look like another one.
class InstanceElement : public Frame {
public:
    NodeKind Kind() const override;

    // The variant of the master this copy shows.
    void SetVariant(std::string value);
    const std::string& Variant() const;
    bool HasVariant() const;

    // Whether the definition this copy named was found. A copy that names a definition the document
    // does not hold draws a marked placeholder rather than nothing, so a document that points at a
    // missing master is seen rather than silently blank.
    void SetResolved(bool value);
    bool Resolved() const;

    // Whether this copy is the one the master came from, which is what the editor marks when it
    // shows the definition and the copy of it in place of each other.
    void SetMainInstance(bool value);
    bool MainInstance() const;

    // Whether this copy has been detached from its master. A detached copy keeps the content it was
    // given and stops following the master, which is what makes one copy able to differ completely
    // from the rest without a definition of its own.
    void SetDetached(bool value);
    bool Detached() const;

    // How many of the master's children this copy replaces with a child of its own.
    void SetOverrideCount(int value);
    int OverrideCount() const;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string variant;
    bool resolved = false;
    bool mainInstance = false;
    bool detached = false;
    int overrideCount = 0;
};

}  // namespace Runtime
