#pragma once

#include "app/Application.h"

namespace Multifile {

// The handlers this program answers a document with. They are kept in a file of their own so that a
// screen can be split across as many files as it is written across: the part of the program that
// answers a click is not the part that opens the window, and a second screen would bring a second
// file of handlers rather than a longer one.
Runtime::HandlerMap RegisterHandlers();

}  // namespace Multifile
