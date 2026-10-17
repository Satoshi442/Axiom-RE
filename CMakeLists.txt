cmake_minimum_required(VERSION 3.22)
project(axiom_re LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(PRELOADER_ANDROID_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/preloader-android")

# --- ImGui ---
add_library(imgui STATIC 
    deps/imgui/imgui.cpp 
    deps/imgui/imgui_draw.cpp 
    deps/imgui/imgui_tables.cpp 
    deps/imgui/imgui_widgets.cpp 
    deps/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC deps/imgui deps/imgui/backends)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_ES3)

# --- Fmt ---
add_subdirectory(deps/fmt)

# --- Capstone ---
set(CAPSTONE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARM64_SUPPORT ON CACHE BOOL "Enable ARM64 support" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "Disable default architectures" FORCE)
set(CAPSTONE_ARM_SUPPORT ON CACHE BOOL "Enable ARM support" FORCE)
add_subdirectory(deps/capstone)

# --- Frida-Gum ---
add_library(frida_gum STATIC IMPORTED)
set_target_properties(frida_gum PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/deps/frida-gum/libfrida-gum.a")

# --- Main Mod Library ---
add_library(axiom_re SHARED
    src/AxiomRE.cpp 
    src/hooks/RenderHook.cpp 
    src/hooks/TouchHook.cpp
    src/ui/AxiomUI.cpp 
    src/analysis/Disasm.cpp 
    src/analysis/Memory.cpp
    src/hooks/Keyboard.cpp
)

target_include_directories(axiom_re PRIVATE
    ${PRELOADER_ANDROID_ROOT}/include 
    deps/imgui 
    deps/imgui/backends
    deps/frida-gum 
    deps/capstone/include 
    deps/fmt/include 
    src
)

# Scoped definitions are preferred over global add_definitions()
target_compile_definitions(axiom_re PRIVATE 
    IMGUI_IMPL_OPENGL_ES3
    CAPSTONE_HAS_ARM64
    CAPSTONE_HAS_ARM
)

# --- Linking ---
find_library(log-lib log)
find_library(z-lib z)
find_library(m-lib m)

target_link_libraries(axiom_re PRIVATE 
    imgui 
    frida_gum 
    capstone 
    fmt::fmt 
    EGL 
    GLESv3 
    android 
    ${log-lib} 
    ${z-lib} 
    ${m-lib} 
    dl 
    c
)

# NOTE: Removed "-Wl,--unresolved-symbols=ignore-all" (See explanation below)
