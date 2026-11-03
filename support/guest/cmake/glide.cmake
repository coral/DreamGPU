# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT EXISTS "${DREAMGPU_GLIDE_SOURCE}/dg-route.h")
  message(FATAL_ERROR "Rust source preparation must supply DREAMGPU_GLIDE_SOURCE")
endif()
set(glide_units grguDepth grguMisc grgu3df grguDraw grguSstGlide grguFog grguTex
  grguLfb GLRender OGLFogTables OGLTextureTables OGLColorAlphaTables TexDB PGUTexture
  Glide GLExtensions PGTexture FormatConversion grguBuffer grguColorAlpha GLutil gsplash g3wrap)
set(glide_sources "${DREAMGPU_GLIDE_SOURCE}/dg-mipmap.c" "${DREAMGPU_GLIDE_SOURCE}/dg-glu.c" "${DREAMGPU_GLIDE_SOURCE}/dg-new.cpp")
foreach(unit IN LISTS glide_units)
  list(APPEND glide_sources "${DREAMGPU_GLIDE_SOURCE}/${unit}.cpp")
endforeach()
foreach(unit clock error library openglext window)
  list(APPEND glide_sources "${DREAMGPU_GLIDE_SOURCE}/platform/windows/${unit}.cpp")
endforeach()
add_custom_command(OUTPUT "${DREAMGPU_GLIDE_SOURCE}/libdgpugl.a"
  COMMAND "${DREAMGPU_MINGW_PREFIX}dlltool" -k -d "${DREAMGPU_GLIDE_SOURCE}/dg-imports.def"
    -l "${DREAMGPU_GLIDE_SOURCE}/libdgpugl.a"
  DEPENDS "${DREAMGPU_GLIDE_SOURCE}/dg-imports.def" VERBATIM)
add_library(glide2x SHARED ${glide_sources} "${DREAMGPU_GLIDE_SOURCE}/Glide2x.def" "${DREAMGPU_GLIDE_SOURCE}/libdgpugl.a")
dreamgpu_cpp(glide2x)
target_compile_options(glide2x PRIVATE -O2 -march=pentium3 -msse -mno-sse2 -mfpmath=sse
  -ffunction-sections -fdata-sections -include "${DREAMGPU_GLIDE_SOURCE}/dg-route.h")
target_compile_definitions(glide2x PRIVATE _cdecl=__cdecl WIN32 _WIN32_WINNT=0x0400 WINVER=0x0400 HAVE_CONFIG_H)
target_include_directories(glide2x PRIVATE "${DREAMGPU_GLIDE_SOURCE}" "${DREAMGPU_GLIDE_SOURCE}/platform/windows")
target_link_options(glide2x PRIVATE -static-libgcc
  -Wl,--gc-sections,--enable-stdcall-fixup,--no-insert-timestamp,--subsystem,windows:4.0)
target_link_libraries(glide2x PRIVATE "${DREAMGPU_GLIDE_SOURCE}/libdgpugl.a" gdi32 user32 winmm)
set_target_properties(glide2x PROPERTIES PREFIX "" LINKER_LANGUAGE C RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/application")
install(TARGETS glide2x RUNTIME DESTINATION application COMPONENT ${DREAMGPU_GUEST_OS})
install(FILES "${DREAMGPU_GLIDE_SOURCE}/COPYING.LGPL-2.1" "${DREAMGPU_GLIDE_SOURCE}/COPYING.SGI-B-2.0"
  DESTINATION licenses/glide COMPONENT ${DREAMGPU_GUEST_OS})
list(APPEND DREAMGPU_RUNTIME_TARGETS glide2x)

# Glide 3 shares the OpenGLide core above (glide2x keeps its exact historical
# list, including the unexported donor g3wrap.cpp). The donor wrapper is
# replaced by the DreamGPU Glide 3 front end and its own export table.
set(glide3_sources ${glide_sources})
list(REMOVE_ITEM glide3_sources "${DREAMGPU_GLIDE_SOURCE}/g3wrap.cpp")
list(APPEND glide3_sources "${DREAMGPU_GLIDE_SOURCE}/dg-glide3.cpp")
add_library(glide3x SHARED ${glide3_sources} "${DREAMGPU_GLIDE_SOURCE}/dg-glide3x.def" "${DREAMGPU_GLIDE_SOURCE}/libdgpugl.a")
dreamgpu_cpp(glide3x)
target_compile_options(glide3x PRIVATE -O2 -march=pentium3 -msse -mno-sse2 -mfpmath=sse
  -ffunction-sections -fdata-sections -include "${DREAMGPU_GLIDE_SOURCE}/dg-route.h")
target_compile_definitions(glide3x PRIVATE _cdecl=__cdecl WIN32 _WIN32_WINNT=0x0400 WINVER=0x0400 HAVE_CONFIG_H)
target_include_directories(glide3x PRIVATE "${DREAMGPU_GLIDE_SOURCE}" "${DREAMGPU_GLIDE_SOURCE}/platform/windows")
set_source_files_properties("${DREAMGPU_GLIDE_SOURCE}/dg-glide3.cpp" PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
# Each export names its exact decorated implementation (_grX@N = dg3_grX@N).
# Without stdcall fixup a prototype whose argument bytes differ cannot link.
target_link_options(glide3x PRIVATE -static-libgcc
  -Wl,--gc-sections,--disable-stdcall-fixup,--no-insert-timestamp,--subsystem,windows:4.0)
target_link_libraries(glide3x PRIVATE "${DREAMGPU_GLIDE_SOURCE}/libdgpugl.a" gdi32 user32 winmm)
set_target_properties(glide3x PROPERTIES PREFIX "" LINKER_LANGUAGE C RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/application")
install(TARGETS glide3x RUNTIME DESTINATION application COMPONENT ${DREAMGPU_GUEST_OS})
list(APPEND DREAMGPU_RUNTIME_TARGETS glide3x)
