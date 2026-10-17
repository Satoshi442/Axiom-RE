cmake_minimum_required(VERSION 3.22)
project(axiom_re LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(PRELOADER_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/preloader-android")

add_library(imgui STATIC deps/imgui/imgui.cpp deps/imgui/imgui_draw.cpp deps/imgui/imgui_tables.cpp deps/imgui/imgui_widgets.cpp deps/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC deps/imgui deps/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

add_subdirectory(deps/fmt)

set(CAPSTONE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
# Enable ARM64 support for Capstone
set(CAPSTONE_ARM64_SUPPORT ON CACHE BOOL "Enable ARM64 support" FORCE)

# If your project disables all architectures by default, you might also need:
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "Disable default architectures" FORCE)
set(CAPSTONE_ARM_SUPPORT ON CACHE BOOL "Enable ARM support" FORCE) # If you also need 32-bit ARM

add_subdirectory(deps/capstone)

add_library(frida_gum STATIC IMPORTED)
set_target_properties(frida_gum PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/deps/frida-gum/libfrida-gum.a")

add_library(axiom_re SHARED
    src/AxiomRE.cpp src/hooks/RenderHook.cpp src/hooks/TouchHook.cpp
    src/ui/AxiomUI.cpp src/analysis/Disasm.cpp src/analysis/Memory.cpp
    src/hooks/Keyboard.cpp
)

target_include_directories(axiom_re PRIVATE
    ${PRELOADER_ANDROID_ROOT}/include deps/imgui deps/imgui/backends
    deps/frida-gum deps/capstone/include deps/fmt/include src
)
target_compile_definitions(axiom_re PRIVATE IMGUI_IMPL_OPENGL_ES3)

find_library(log-lib log)
target_link_libraries(axiom_re PRIVATE imgui frida_gum capstone fmt::fmt EGL GLESv3 android ${log-lib} dl)
target_link_options(axiom_re PRIVATE -Wl,--unresolved-symbols=ignore-all)
