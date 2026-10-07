#include "compiler/semantic/UiCompiler.h"
#include "rendering/text/TextMeasurer.h"
#include "ui/element/Element.h"
#include "ui/factory/UiFactory.h"

#include <chrono>
#include <iostream>
#include <string>

namespace {

class FakeMeasurer : public Runtime::TextMeasurer {
public:
    Runtime::Size MeasureText(std::string_view text, const Runtime::TextStyle& style, float) override {
        return Runtime::Size{static_cast<float>(text.size()) * style.fontSize * 0.5f, style.fontSize};
    }
};

}  // namespace

int main() {
    const std::string xml = R"(<window width="800" height="600" title="Bench">
    <column padding="24" spacing="12" alignment="center">
        <text text="Welcome" fontSize="32"/>
        <button text="Start" width="160px" height="44px" onClick="start"/>
    </column>
</window>)";
    constexpr int COMPILE_ITERATIONS = 1000;
    const auto compileStart = std::chrono::steady_clock::now();
    for (int index = 0; index < COMPILE_ITERATIONS; ++index) {
        const Runtime::CompileResult compiled = Runtime::CompileUi(Runtime::SourceFile{"bench.xml", xml});
        if (!compiled.Ok()) {
            std::cerr << "compile failed\n";
            return 1;
        }
    }
    const auto compileEnd = std::chrono::steady_clock::now();
    const double compileUs =
        std::chrono::duration<double, std::micro>(compileEnd - compileStart).count() / COMPILE_ITERATIONS;

    const Runtime::CompileResult compiled = Runtime::CompileUi(Runtime::SourceFile{"bench.xml", xml});
    std::unique_ptr<Runtime::Element> root = Runtime::Instantiate(*compiled.root, {});
    FakeMeasurer measurer;
    constexpr int LAYOUT_ITERATIONS = 10000;
    const auto layoutStart = std::chrono::steady_clock::now();
    for (int index = 0; index < LAYOUT_ITERATIONS; ++index) {
        root->Measure(measurer, 800.0f, 600.0f, true);
        root->Arrange(Runtime::Rect{0.0f, 0.0f, 800.0f, 600.0f});
    }
    const auto layoutEnd = std::chrono::steady_clock::now();
    const double layoutUs =
        std::chrono::duration<double, std::micro>(layoutEnd - layoutStart).count() / LAYOUT_ITERATIONS;

    std::cout << "xml compile: " << COMPILE_ITERATIONS << " iterations, " << compileUs << " us/op\n";
    std::cout << "layout: " << LAYOUT_ITERATIONS << " iterations, " << layoutUs << " us/op\n";
    return 0;
}
