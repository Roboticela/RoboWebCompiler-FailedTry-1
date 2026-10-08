#pragma once

#include <string>
#include <string_view>

namespace Runtime {

// The case a stretch of text is written in, named the way a document names it: none, uppercase,
// lowercase, or capitalize. It lives on its own because a block of text applies the case of the
// block to its own text and the case of each run to that run, so the one reading of a case word is
// what both of them use.
std::string TextInCase(std::string_view text, std::string_view textCase);

}  // namespace Runtime
