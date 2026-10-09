#pragma once

#include "components/frame/Frame.h"

#include <string>
#include <string_view>

namespace Runtime {

// A component is the definition of something that can be placed more than once. It is a frame in
// every way a document cares about: it holds children, it lays them out, it carries a background and
// a border, and it can be told to hug its content or fill its parent. What makes it a definition
// rather than an ordinary box is the name it answers to and, when it sits in a set, the variant of
// the component it draws. An instance is placed by naming it, so the name is the whole reason the
// node exists.
//
// The master is drawn on the canvas exactly as its content is drawn, with a small mark in its
// corner so a definition is told apart from the frame it was built out of.
class ComponentElement : public Frame {
public:
    NodeKind Kind() const override;

    // The variant of the component this definition draws. A component that is not one of several
    // variants answers with nothing, which is what an instance with no variant asks for.
    void SetVariant(std::string value);
    const std::string& Variant() const;
    bool HasVariant() const;

    void SetDescription(std::string value);
    const std::string& Description() const;

    // Whether the corner mark is drawn. A definition that is drawn as part of the finished screen
    // can hide it, the way a document hides the outline of anything else it does not want marked.
    void SetBadgeVisible(bool value);
    bool BadgeVisible() const;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string variant;
    std::string description;
    bool badgeVisible = true;
};

// A set gathers every variant of one component under a single name. It is a frame that holds
// component definitions, so it lays them out and draws them the way any other box does. What it adds
// is the one question an instance asks of it: which of the definitions it holds draws the variant a
// copy was told to show.
class ComponentSetElement : public Frame {
public:
    NodeKind Kind() const override;

    void SetDescription(std::string value);
    const std::string& Description() const;

    int VariantCount() const;
    // The definition of the variant at this position, or null when the position is out of range.
    // Only the component definitions a set holds are counted, so a set with a label beside its
    // variants reports the variants rather than everything it holds.
    ComponentElement* VariantAt(int index);
    // The definition that draws this variant, or the first definition the set holds when no variant
    // is named or none matches.
    ComponentElement* VariantNamed(std::string_view name);

    void SetBadgeVisible(bool value);
    bool BadgeVisible() const;

protected:
    void OnPaint(Renderer& renderer) override;

private:
    std::string description;
    bool badgeVisible = true;
};

}  // namespace Runtime
