#cmake

cmake_minimum_required(VERSION 3.14)

project ("Kraut")

# Build options:
# - Default build is light and fast: KrautCLI + KrautPreview (+ KrautFoundation/KrautGenerator).
# - The full Qt-based editor (aeEditor) and its editor-only engine libraries
#   (KrautGraphics, KrautEditorBasics, KrautEditorRenderAPI_GL, TreePlugin) are
#   only built on demand with -DKRAUT_BUILD_EDITOR=ON.
option (KRAUT_BUILD_EDITOR "Build the full Qt-based aeEditor and its editor-only engine libraries" OFF)
option (KRAUT_BUILD_PREVIEW "Build the SDL2+ImGui tree preview tool (fetches SDL2/ImGui/GLEW at configure time)" ON)

message (STATUS "KRAUT_BUILD_EDITOR: ${KRAUT_BUILD_EDITOR}")
message (STATUS "KRAUT_BUILD_PREVIEW: ${KRAUT_BUILD_PREVIEW}")

#Clear Targets.txt and Tests.txt
file(WRITE ${CMAKE_BINARY_DIR}/Targets.txt "")
file(WRITE ${CMAKE_BINARY_DIR}/Tests.txt "")

#Clear cached qt dirs
set (Qt5Core_DIR "" CACHE PATH "" FORCE)
set (Qt5Gui_DIR "" CACHE PATH "" FORCE)
set (Qt5Widgets_DIR "" CACHE PATH "" FORCE)
set (Qt5Network_DIR "" CACHE PATH "" FORCE)

message (STATUS "Adding SubDirectory 'Code'")
add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/Code)

message (STATUS "Adding SubDirectory 'Tools'")
add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/Tools)
