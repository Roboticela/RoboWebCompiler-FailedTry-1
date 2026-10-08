cmake_minimum_required(VERSION 3.24)

project(Runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "Runtime builds for Windows only. DirectX is the graphics backend.")
endif()

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
endif()

add_library(RuntimeXml STATIC
    xml/diagnostics/Diagnostic.cpp
    xml/diagnostics/Diagnostic.h
    xml/lexer/XmlLexer.cpp
    xml/lexer/XmlLexer.h
    xml/parser/XmlParser.cpp
    xml/parser/XmlParser.h
    xml/ast/XmlAst.h
    xml/serializer/XmlSerializer.cpp
    xml/serializer/XmlSerializer.h
)
target_include_directories(RuntimeXml PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(RuntimeCompiler STATIC
    compiler/types/ValueParser.cpp
    compiler/types/ValueParser.h
    compiler/semantic/Suggestions.cpp
    compiler/semantic/Suggestions.h
    compiler/semantic/ComponentCatalog.cpp
    compiler/semantic/ComponentCatalog.h
    compiler/semantic/DocumentPass.cpp
    compiler/semantic/DocumentPass.h
    compiler/semantic/Foundations.cpp
    compiler/semantic/Foundations.h
    compiler/semantic/UiCompiler.cpp
    compiler/semantic/UiCompiler.h
    compiler/ir/FormatIr.cpp
    compiler/ir/UiIr.h
)
target_include_directories(RuntimeCompiler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(RuntimeCompiler PUBLIC RuntimeXml)

add_library(RuntimeUi STATIC
    ui/element/Element.cpp
    ui/element/Element.h
    ui/input/PointerInput.cpp
    ui/input/PointerInput.h
    ui/factory/UiFactory.cpp
    ui/factory/UiFactory.h
    components/frame/Frame.cpp
    components/frame/Frame.h
    components/boolean/BooleanElement.cpp
    components/boolean/BooleanElement.h
    components/group/GroupElement.cpp
    components/group/GroupElement.h
    components/image/ImageElement.cpp
    components/image/ImageElement.h
    components/rectangle/RectangleElement.cpp
    components/rectangle/RectangleElement.h
    components/polygon/PolygonElement.cpp
    components/polygon/PolygonElement.h
    components/star/StarElement.cpp
    components/star/StarElement.h
    components/vector/VectorElement.cpp
    components/vector/VectorElement.h
    components/svg/SvgElement.cpp
    components/svg/SvgElement.h
    components/ellipse/EllipseElement.cpp
    components/ellipse/EllipseElement.h
    components/line/LineElement.cpp
    components/line/LineElement.h
    components/arrow/ArrowElement.cpp
    components/arrow/ArrowElement.h
    components/primitive/PrimitiveElement.cpp
    components/primitive/PrimitiveElement.h
    components/layout/WindowElement.cpp
    components/layout/WindowElement.h
    components/layout/BoardElement.cpp
    components/layout/BoardElement.h
    components/text/TextElement.cpp
    components/text/TextElement.h
    components/text/SpanElement.cpp
    components/text/SpanElement.h
    components/text/RichTextElement.cpp
    components/text/RichTextElement.h
    rendering/renderer/Renderer.h
    rendering/renderer/OutlineRecorder.cpp
    rendering/renderer/OutlineRecorder.h
    rendering/renderer/StrokePattern.cpp
    rendering/renderer/StrokePattern.h
    rendering/text/TextMeasurer.h
    rendering/text/TextStyle.h
    rendering/text/TextRun.h
    rendering/text/TextTransform.cpp
    rendering/text/TextTransform.h
    rendering/text/TextLayout.cpp
    rendering/text/TextLayout.h
    rendering/path/PathBoolean.cpp
    rendering/path/PathBoolean.h
    rendering/path/PathCurve.cpp
    rendering/path/PathCurve.h
    rendering/image/ImageAdjust.cpp
    rendering/image/ImageAdjust.h
    rendering/image/ImageDecoder.cpp
    rendering/image/SvgImage.cpp
    rendering/image/ImageDecoder.h
    rendering/image/ImageCodec.h
    rendering/image/Inflate.cpp
    rendering/image/Inflate.h
    rendering/image/StillImage.cpp
    rendering/image/WebpImage.cpp
    rendering/image/Vp8LImage.cpp
    rendering/image/Vp8LImage.h
    rendering/image/Vp8Image.cpp
    rendering/image/Vp8Image.h
    rendering/image/BmpImage.cpp
    rendering/image/IcoImage.cpp
    rendering/image/AvifImage.cpp
    engine/motion/Motion.cpp
    engine/motion/Motion.h
)
target_include_directories(RuntimeUi PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# The image readers are the runtime's own, written in portable code, so the user interface is built
# with nothing but the text layout of the platform underneath it.
target_link_libraries(RuntimeUi PUBLIC RuntimeCompiler dwrite)
# A file a document names is looked for beside the project among the places it may be, since a
# document written against the layout of the project names its art that way, and the fonts are read
# from the same project. It is public because every program built here links the user interface.
target_compile_definitions(RuntimeUi PUBLIC "RUNTIME_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

add_library(RuntimePlatform STATIC
    platform/windows/Win32Window.cpp
    platform/windows/Win32Window.h
    platform/windows/Dx11Renderer.cpp
    platform/windows/Dx11Renderer.h
    platform/windows/WicImageProvider.cpp
    platform/windows/WicImageProvider.h
)
target_include_directories(RuntimePlatform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# The AV1 codec that fills an AVIF in ships beside Windows rather than inside it, so the reader that
# reaches it belongs to the layer that knows the platform, and the windowscodecs and ole32 the
# platform reader is reached through are linked there rather than by the user interface.
target_link_libraries(RuntimePlatform PUBLIC RuntimeUi d3d11 dxgi d2d1 dwrite windowscodecs ole32)
target_compile_definitions(RuntimePlatform PUBLIC "RUNTIME_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

add_library(RuntimeApp STATIC
    app/Application.cpp
    app/Application.h
)
target_include_directories(RuntimeApp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(RuntimeApp PUBLIC RuntimePlatform)

add_executable(Hello examples/Hello/Main.cpp)
target_link_libraries(Hello PRIVATE RuntimeApp)
target_compile_definitions(Hello PRIVATE "HELLO_XML_PATH=\"${CMAKE_SOURCE_DIR}/examples/Hello/Hello.xml\"")

add_executable(myui tools/cli/Main.cpp)
target_link_libraries(myui PRIVATE RuntimeApp)

add_library(RuntimeDesigner STATIC
    designer/model/DesignDocument.cpp
    designer/model/DesignDocument.h
)
target_include_directories(RuntimeDesigner PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(RuntimeDesigner PUBLIC RuntimeCompiler)

add_executable(RoboUIDesigner
    designer/Main.cpp
    designer/assets/IconPack.cpp
    designer/assets/IconPack.h
    designer/shell/DesignerApp.cpp
    designer/shell/DesignerApp.h
    designer/tools/EditorTool.h
)
target_link_libraries(RoboUIDesigner PRIVATE RuntimeDesigner RuntimeApp ole32 shell32 uuid)
target_compile_definitions(RoboUIDesigner PRIVATE "DESIGNER_HELLO_XML=\"${CMAKE_SOURCE_DIR}/examples/Hello/Hello.xml\"" "RUNTIME_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

# The tests link the platform as well as the user interface, because the one reader that is not the
# runtime's own is the platform's: a test has to be able to offer it the same way a program does.
add_executable(RuntimeTests tests/Main.cpp tests/TestHarness.h)
target_link_libraries(RuntimeTests PRIVATE RuntimePlatform RuntimeDesigner)
target_compile_definitions(RuntimeTests PRIVATE "RUNTIME_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\"")

add_executable(RuntimeBenchmarks benchmarks/Main.cpp)
target_link_libraries(RuntimeBenchmarks PRIVATE RuntimeUi)

enable_testing()
add_test(NAME UnitTests COMMAND RuntimeTests)
add_test(NAME HelloSelfTest COMMAND Hello --self-test)
add_test(NAME DesignerSelfTest COMMAND RoboUIDesigner --self-test)
set_tests_properties(HelloSelfTest DesignerSelfTest PROPERTIES TIMEOUT 60)
