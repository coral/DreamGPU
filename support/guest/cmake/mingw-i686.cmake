# SPDX-License-Identifier: GPL-2.0-or-later
# Compiler executables are cross tools running on the build host. Guest code
# targets Win98/NT5, never the host running Cargo or CMake.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_SYSTEM_VERSION 4.0)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(DREAMGPU_MINGW_PREFIX "i686-w64-mingw32-" CACHE STRING "Pinned MinGW tool prefix")
set(CMAKE_C_COMPILER "${DREAMGPU_MINGW_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${DREAMGPU_MINGW_PREFIX}g++")
set(CMAKE_AR "${DREAMGPU_MINGW_PREFIX}ar")
set(CMAKE_RANLIB "${DREAMGPU_MINGW_PREFIX}ranlib")
set(CMAKE_RC_COMPILER "${DREAMGPU_MINGW_PREFIX}windres")
set(CMAKE_C_STANDARD_LIBRARIES "" CACHE STRING "Explicit guest import libraries" FORCE)
set(CMAKE_CXX_STANDARD_LIBRARIES "" CACHE STRING "Explicit guest import libraries" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Actual cross-compiler commands" FORCE)
