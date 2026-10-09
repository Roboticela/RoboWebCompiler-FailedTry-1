#include "app/LinkRoute.h"

namespace Runtime {
namespace {

// A name is a name only when something is written in it: whitespace around it is not a place, and a
// link written with an empty name is a link that names nowhere rather than one that names an empty
// page.
std::string Trimmed(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

LinkRoute RouteLink(const std::string& href, const std::string& to, const std::string& target) {
    const std::string page = Trimmed(href);
    if (!page.empty()) {
        return LinkRoute{LinkAction::OPEN, page};
    }
    const std::string screen = Trimmed(to);
    if (screen.empty()) {
        return LinkRoute{};
    }
    const std::string where = Trimmed(target);
    const bool ownWindow = where == "window" || where == "blank";
    return LinkRoute{ownWindow ? LinkAction::RAISE : LinkAction::SHOW, screen};
}

}  // namespace Runtime
