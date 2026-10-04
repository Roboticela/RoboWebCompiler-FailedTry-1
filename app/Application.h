#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Runtime {

class Element;

class UiContext {
public:
    void Bind(Element* rootElement);
    void Invalidate();
    bool ConsumeInvalidated();
    Element* Find(std::string_view id) const;

private:
    Element* root = nullptr;
    bool invalidated = false;
};

using ClickHandler = std::function<void(UiContext&)>;
using HandlerMap = std::unordered_map<std::string, ClickHandler>;

struct RunOptions {
    bool selfTest = false;
};

class Application {
public:
    int Run(const std::filesystem::path& xmlPath, const HandlerMap& handlers, RunOptions options = {});
};

}  // namespace Runtime
