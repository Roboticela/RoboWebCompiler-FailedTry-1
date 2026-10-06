#pragma once

#include "components/frame/Frame.h"

namespace Runtime {

class WindowElement : public Frame {
public:
    WindowElement();

    NodeKind Kind() const override;
    Element* HitTest(Point point) override;

    void SetTitle(std::string value);
    const std::string& Title() const;

private:
    std::string title;
};

}  // namespace Runtime
