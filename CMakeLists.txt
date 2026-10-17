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

# --- Capstone 6 (AArch64) ---
set(CAPSTONE_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_LEGACY_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "Disable default architectures" FORCE)
set(CAPSTONE_ARM_SUPPORT ON CACHE BOOL "Enable ARM support" FORCE)
set(CAPSTONE_AARCH64_SUPPORT ON CACHE BOOL "Enable AArch64 support" FORCE)
add_subdirectory(deps/capstone)

# Capstone 6 also builds a fuzzer executable; keep it out of the Android build.
if(TARGET fuzz_disasm)
    set_target_properties(fuzz_disasm PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

# In v6 `capstone` is only an object library; the static lib is `capstone_static`.
if(TARGET capstone_static)
    set(CAPSTONE_LIB capstone_static)
else()
    set(CAPSTONE_LIB capstone)
endif()

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
# (Capstone defines its own CAPSTONE_HAS_* macros internally.)
target_compile_definitions(axiom_re PRIVATE 
    IMGUI_IMPL_OPENGL_ES3
)

# --- Linking ---
find_library(log-lib log)
find_library(z-lib z)
find_library(m-lib m)

target_link_libraries(axiom_re PRIVATE 
    imgui 
    frida_gum 
    ${CAPSTONE_LIB} 
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
