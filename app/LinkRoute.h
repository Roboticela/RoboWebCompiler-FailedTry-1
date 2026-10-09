#pragma once

#include <cstdint>
#include <string>

namespace Runtime {

// What a link asks for once the names it carries are read. A link does not open anything by itself:
// it names a place, and what going there means is the program's to say. This is the decision the
// program is handed -- which of the two names it was given is the one that is used, and whether the
// screen it names is shown where the link stands or brought forward in a window of its own.
enum class LinkAction : std::uint8_t {
    // The link names nowhere: a shape drawn as a link that does nothing, which is what a document
    // whose author is still deciding where a link goes holds.
    NONE,
    // Open the name with the system, which is what a page, a folder, or a file means.
    OPEN,
    // Show the named screen in the window the link stands in.
    SHOW,
    // Bring the window that already shows the named screen to the front.
    RAISE,
};

struct LinkRoute {
    LinkAction action = LinkAction::NONE;
    std::string name;
};

// What a link means. `href` is a name the system opens; `to` is a screen of the same document, so a
// link that carries both opens the page rather than showing the screen, because a page is the thing
// that cannot be shown here. `target` says where a screen belongs: `self`, the default, is the place
// the link stands, and `window` or `blank` is a window of its own, which is the window already
// showing the screen when one is open.
LinkRoute RouteLink(const std::string& href, const std::string& to, const std::string& target);

}  // namespace Runtime
