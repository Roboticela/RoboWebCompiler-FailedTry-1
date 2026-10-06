#pragma once

#include "components/frame/Frame.h"

namespace Runtime {

class BoardElement : public Frame {
public:
    BoardElement();
    NodeKind Kind() const override;
};

}  // namespace Runtime
