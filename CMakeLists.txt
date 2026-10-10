cmake_minimum_required(VERSION 3.22)
project(axiom_re LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(PRELOADER_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/preloader-android")

# ------------------------------------------------------------------
# fmtlib (required by preloader-android)
# ------------------------------------------------------------------
add_subdirectory(deps/fmt)

# ------------------------------------------------------------------
# Dear ImGui
# ------------------------------------------------------------------
add_library(imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC deps/imgui deps/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

# ------------------------------------------------------------------
# Capstone (Disassembly)
# ------------------------------------------------------------------
set(CAPSTONE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/capstone)

# ------------------------------------------------------------------
# Frida GUM (prebuilt - headers are at root, no include/ subfolder)
# ------------------------------------------------------------------
add_library(frida_gum STATIC IMPORTED)
set_target_properties(frida_gum PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/deps/frida-gum/libfrida-gum.a"
)

# ------------------------------------------------------------------
# Axiom-RE
# ------------------------------------------------------------------
add_library(axiom_re SHARED
    src/AxiomRE.cpp
    src/hooks/RenderHook.cpp
    src/ui/AxiomUI.cpp
    src/analysis/Disasm.cpp
)

target_include_directories(axiom_re PRIVATE
    ${PRELOADER_ANDROID_ROOT}/include
    deps/fmt/include
    deps/imgui
    deps/imgui/backends
    deps/frida-gum
    deps/capstone/include
    src
)

target_compile_definitions(axiom_re PRIVATE IMGUI_IMPL_OPENGL_ES3)

find_library(log-lib log)

target_link_libraries(axiom_re PRIVATE
    imgui
    frida_gum
    capstone
    fmt::fmt
    EGL
    GLESv3
    ${log-lib}
    dl
)

target_link_options(axiom_re PRIVATE
    -Wl,--unresolved-symbols=ignore-all
)
)
