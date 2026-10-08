cmake_minimum_required(VERSION 3.22)
project(axiom_re LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(PRELOADER_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/preloader-android")

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
# Dobby
# ------------------------------------------------------------------
set(DOBBY_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/dobby)

# ------------------------------------------------------------------
# Axiom-RE (UPDATED PATHS FOR YOUR NEW FOLDER STRUCTURE)
# ------------------------------------------------------------------
add_library(axiom_re SHARED
    src/AxiomRE.cpp
    src/hooks/RenderHook.cpp
    src/ui/AxiomUI.cpp
)

target_include_directories(axiom_re PRIVATE
    ${PRELOADER_ANDROID_ROOT}/include
    deps/imgui
    deps/imgui/backends
    deps/dobby/include
    src    # <--- THIS IS IMPORTANT: It lets AxiomRE.cpp find "hooks/" and "ui/"
)

target_compile_definitions(axiom_re PRIVATE IMGUI_IMPL_OPENGL_ES3)

find_library(log-lib log)
target_link_libraries(axiom_re PRIVATE imgui dobby EGL GLESv3 ${log-lib} dl)
target_link_options(axiom_re PRIVATE -Wl,--unresolved-symbols=ignore-all)
