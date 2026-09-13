# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT EXISTS "${DREAMGPU_WINE_SOURCE}/config.mk")
  message(FATAL_ERROR "Rust source preparation must supply DREAMGPU_WINE_SOURCE")
endif()
find_program(DREAMGPU_MAKE make REQUIRED)
find_program(DREAMGPU_NASM nasm REQUIRED)
find_program(DREAMGPU_HOST_RUSTC rustc REQUIRED)
set(recorder "${CMAKE_BINARY_DIR}/host-tools/record-compiler")
add_custom_command(OUTPUT "${recorder}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/host-tools"
  COMMAND "${DREAMGPU_HOST_RUSTC}" --edition=2021 -O "${DREAMGPU_CMAKE}/record-compiler.rs" -o "${recorder}"
  DEPENDS "${DREAMGPU_CMAKE}/record-compiler.rs" VERBATIM)
set(wine_cc "")
foreach(argument "${recorder}" "${CMAKE_BINARY_DIR}/wine-compile-commands" "${CMAKE_C_COMPILER}")
  string(REPLACE "'" "'\\''" escaped "${argument}")
  string(APPEND wine_cc "'${escaped}' ")
endforeach()
set(wine_dlls wined3d.dll winedd.dll wined8.dll wined9.dll)
if(DREAMGPU_GUEST_OS STREQUAL "win98")
  list(APPEND wine_dlls ddraw_98.dll d3d8_98.dll d3d9_98.dll)
else()
  list(APPEND wine_dlls ddraw_xp.dll d3d8_xp.dll d3d9_xp.dll)
endif()
set(wine_outputs)
set(wine_copy_commands)
foreach(dll IN LISTS wine_dlls)
  if(dll MATCHES "_(98|xp)\\.dll$")
    set(destination switchers)
  else()
    set(destination application)
  endif()
  list(APPEND wine_outputs "${CMAKE_BINARY_DIR}/${destination}/${dll}")
  list(APPEND wine_copy_commands COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${DREAMGPU_WINE_SOURCE}/${dll}" "${CMAKE_BINARY_DIR}/${destination}/${dll}")
  install(FILES "${CMAKE_BINARY_DIR}/${destination}/${dll}" DESTINATION ${destination} COMPONENT ${DREAMGPU_GUEST_OS})
endforeach()
file(GLOB_RECURSE wine_inputs CONFIGURE_DEPENDS "${DREAMGPU_WINE_SOURCE}/*.c" "${DREAMGPU_WINE_SOURCE}/*.h"
  "${DREAMGPU_WINE_SOURCE}/*.S" "${DREAMGPU_WINE_SOURCE}/*.asm" "${DREAMGPU_WINE_SOURCE}/*.mk")
add_custom_command(OUTPUT ${wine_outputs}
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/application" "${CMAKE_BINARY_DIR}/switchers"
  COMMAND "${DREAMGPU_MINGW_PREFIX}dlltool" -k -d "${DREAMGPU_WINE_SOURCE}/dg-imports.def"
    -l "${DREAMGPU_WINE_SOURCE}/libdgpugl.a"
  COMMAND "${DREAMGPU_MAKE}" -j2 "CC=${wine_cc}" ${wine_dlls}
  ${wine_copy_commands}
  WORKING_DIRECTORY "${DREAMGPU_WINE_SOURCE}"
  DEPENDS "${recorder}" ${wine_inputs} "${DREAMGPU_WINE_SOURCE}/Makefile" "${DREAMGPU_WINE_SOURCE}/dg-imports.def"
  VERBATIM)
add_custom_target(dreamgpu-wine DEPENDS ${wine_outputs})
install(FILES "${DREAMGPU_WINE_SOURCE}/LICENSE" "${DREAMGPU_WINE_SOURCE}/LICENSE.nocrt"
  "${DREAMGPU_WINE_SOURCE}/LICENSE.pthread9x" "${DREAMGPU_WINE_SOURCE}/LICENSE.pthread9x-source-notices"
  "${DREAMGPU_WINE_SOURCE}/COPYING.GPL-2.0" DESTINATION licenses/wine COMPONENT ${DREAMGPU_GUEST_OS})
list(APPEND DREAMGPU_RUNTIME_TARGETS dreamgpu-wine)
