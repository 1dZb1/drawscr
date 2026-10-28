cmake_minimum_required(VERSION 3.23)
project(Drawscr VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00
)
add_compile_options(/W4 /permissive- /utf-8 /EHsc)

add_library(drawscr_core STATIC
    src/model.cpp
    src/text_transaction.cpp
    src/text_layout.cpp
    src/text_overlay.cpp
    src/text_raster.cpp
    src/preferences.cpp
    src/image.cpp
    src/canvas.cpp
    src/gif_encoder.cpp
    src/palette.cpp
    src/theme.cpp
    src/recorder.cpp
    src/trigger.cpp
    src/startup.cpp
)
target_include_directories(drawscr_core PUBLIC src)
target_link_libraries(drawscr_core PUBLIC gdiplus windowscodecs ole32 propsys advapi32 imm32)

set_property(SOURCE src/app.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/drawscr.ico")
add_executable(drawscr WIN32
    src/main.cpp
    src/launcher_view.cpp
    src/session.cpp
    src/text_editor.cpp
    src/toolbar.cpp
    src/record_ui.cpp
    src/shortcut_ui.cpp
    src/app.rc
)
target_link_libraries(drawscr PRIVATE drawscr_core user32 gdi32 shell32 comctl32 comdlg32 dwmapi)
target_link_options(drawscr PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")

