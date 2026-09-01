#cmake

#
# KrautPreview - SDL2 + Dear ImGui + OpenGL 3.3 preview for generated trees.
# Part of the default (light) build. Disable with -DKRAUT_BUILD_PREVIEW=OFF.
# Fetches SDL2, Dear ImGui and stb_image_write at configure time (requires network once).
# The option itself is declared in the root CMakeLists.txt.
#

if (NOT KRAUT_BUILD_PREVIEW)
  return ()
endif ()

include (FetchContent)

# SDL2 (built from source; static so the tool binaries are self-contained
# when vendored into engine builds)
set (SDL_SHARED OFF CACHE BOOL "" FORCE)
set (SDL_STATIC ON CACHE BOOL "" FORCE)
set (SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare (
  SDL2
  URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.5/SDL2-2.30.5.zip
)
FetchContent_MakeAvailable (SDL2)

# Dear ImGui (no build system, sources compiled directly)
FetchContent_Declare (
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.90.9
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable (imgui)

# GLEW (full GL 3.3 core loader; imgui's embedded loader lacks FBOs)
set (glew-cmake_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set (ONLY_LIBS ON CACHE BOOL "" FORCE)
FetchContent_Declare (
  glew
  GIT_REPOSITORY https://github.com/Perlmint/glew-cmake.git
  GIT_TAG glew-cmake-2.2.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable (glew)

# stb_image + stb_image_write (single headers, downloaded into the build tree)
foreach (STB_NAME stb_image stb_image_write)
  set (STB_HEADER "${CMAKE_BINARY_DIR}/_deps/stb/${STB_NAME}.h")
  if (NOT EXISTS "${STB_HEADER}")
    file (DOWNLOAD
      "https://raw.githubusercontent.com/nothings/stb/master/${STB_NAME}.h"
      "${STB_HEADER}"
      SHOW_PROGRESS
    )
  endif ()
endforeach ()

set (IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# reuse the KrautCLI core (descriptor IO + generation pipeline)
set (KRAUTCLI_DIR "${CMAKE_SOURCE_DIR}/Tools/KrautCLI")

add_executable (KrautPreview
  main.cpp
  TextureLoader.cpp
  ${IMGUI_SOURCES}
  ${KRAUTCLI_DIR}/TreeFile.cpp
  ${KRAUTCLI_DIR}/Pipeline.cpp
)

target_include_directories (KrautPreview PRIVATE
  ${KRAUTCLI_DIR}
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${CMAKE_BINARY_DIR}/_deps/stb
)

target_link_libraries (KrautPreview
  KrautFoundation
  KrautGenerator
  SDL2::SDL2
  SDL2::SDL2main
  libglew_static
)

if (WIN32)
  target_link_libraries (KrautPreview opengl32)
elseif (APPLE)
  target_link_libraries (KrautPreview "-framework OpenGL")
else ()
  target_link_libraries (KrautPreview GL)
endif ()

target_compile_definitions (KrautPreview PRIVATE
  GLEW_STATIC
  IMGUI_IMPL_OPENGL_LOADER_GLEW
)

# (static SDL2: no runtime library to copy next to the exe)

