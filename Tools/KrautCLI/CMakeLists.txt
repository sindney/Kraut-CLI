#cmake

#
# KrautCLI - headless command line interface for Kraut tree generation.
# Links only KrautFoundation + KrautGenerator (no Qt, no OpenGL).
#

if (POLICY CMP0043)
  cmake_policy(SET CMP0043 NEW)
endif()

SET (LINK_LIBRARIES
  KrautFoundation
  KrautGenerator
)

include (${CMAKE_SOURCE_DIR}/Code/BuildSystem/CMake/CMAKE_Application.txt)
