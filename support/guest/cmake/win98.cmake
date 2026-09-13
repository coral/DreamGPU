# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT EXISTS "${DREAMGPU_VMDISP_SOURCE}/makefile.dreamgpu" OR
   NOT EXISTS "${DREAMGPU_WATCOM_ROOT}/binl64/wmake")
  message(FATAL_ERROR "Rust preparation must provide patched VMDISP source and pinned Linux Watcom tools")
endif()
set(win98_objects)
foreach(unit packet32 owner32 memory32 cursor32 channel32 blt32)
  add_library(dg9_${unit} OBJECT "${DREAMGPU_GUEST}/win9x/${unit}.cpp")
  dreamgpu_freestanding(dg9_${unit})
  target_compile_options(dg9_${unit} PRIVATE -Os -Werror -mpreferred-stack-boundary=2 -mincoming-stack-boundary=2
    -fno-asynchronous-unwind-tables -fno-unwind-tables)
  set(obj "${DREAMGPU_VMDISP_SOURCE}/${unit}.obj")
  add_custom_command(OUTPUT "${obj}" COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_OBJECTS:dg9_${unit}>" "${obj}" DEPENDS dg9_${unit} VERBATIM)
  list(APPEND win98_objects "${obj}")
endforeach()
set(driver_directory "${CMAKE_BINARY_DIR}/drivers/win98")
set(driver_outputs "${driver_directory}/dgpumini.drv" "${driver_directory}/dgpumini.vxd")
file(GLOB win98_boundary_inputs CONFIGURE_DEPENDS "${DREAMGPU_VMDISP_SOURCE}/*.c"
  "${DREAMGPU_VMDISP_SOURCE}/*.h" "${DREAMGPU_VMDISP_SOURCE}/*.asm")
add_custom_command(OUTPUT ${driver_outputs}
  COMMAND "${CMAKE_COMMAND}" -E env "WATCOM=${DREAMGPU_WATCOM_ROOT}"
    "PATH=${DREAMGPU_WATCOM_ROOT}/binl64:$ENV{PATH}"
    "INCLUDE=${DREAMGPU_WATCOM_ROOT}/h:${DREAMGPU_WATCOM_ROOT}/h/win"
    "${DREAMGPU_WATCOM_ROOT}/binl64/wmake" -a -h -f makefile.dreamgpu dgpumini.drv dgpumini.vxd
    "FIXLINK_EXE=./fixlink-tool"
    "FIXLINK_CC=gcc -include strings.h -Dstricmp=strcasecmp fixlink/fixlink.c -o fixlink-tool"
    "CFLAGS=-q -wx -s -zu -zls -6 -fp6 -fo=.obj -fi=stdint.h"
    "CFLAGS32=-q -wx -s -zls -mf -DVXD32 -fpi87 -ei -oeatxhn -6s -fp6 -fo=.obj"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${driver_directory}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different dgpumini.drv dgpumini.vxd "${driver_directory}"
  WORKING_DIRECTORY "${DREAMGPU_VMDISP_SOURCE}"
  DEPENDS ${win98_objects} ${win98_boundary_inputs} "${DREAMGPU_VMDISP_SOURCE}/makefile.dreamgpu"
  VERBATIM)
add_custom_target(dreamgpu-win98-driver DEPENDS ${driver_outputs})
install(FILES ${driver_outputs} "${DREAMGPU_GUEST}/win9x/dg9x.inf" DESTINATION drivers/win98 COMPONENT win98)
install(FILES "${DREAMGPU_VMDISP_SOURCE}/LICENSE" DESTINATION licenses/win98 COMPONENT win98)
list(APPEND DREAMGPU_RUNTIME_TARGETS dreamgpu-win98-driver)

# Diagnostic source is prepared independently with the checked Win16 ICD patch.
# Its compiler/linker scratch files never share paths with the production tree.
if(NOT EXISTS "${DREAMGPU_VMDISP_ICD_SOURCE}/makefile.dreamgpu" OR
   NOT EXISTS "${DREAMGPU_VMDISP_ICD_SOURCE}/dg-icd16.h")
  message(FATAL_ERROR "Rust preparation must provide the separate Win98 ICD source")
endif()
set(win98_icd_objects)
foreach(unit packet32 owner32 memory32 cursor32 channel32 blt32)
  set(obj "${DREAMGPU_VMDISP_ICD_SOURCE}/${unit}.obj")
  add_custom_command(OUTPUT "${obj}" COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "$<TARGET_OBJECTS:dg9_${unit}>" "${obj}" DEPENDS dg9_${unit} VERBATIM)
  list(APPEND win98_icd_objects "${obj}")
endforeach()
set(icd_driver_directory "${CMAKE_BINARY_DIR}/diagnostics/icd/drivers/win98")
set(icd_driver_outputs "${icd_driver_directory}/dgpumini.drv" "${icd_driver_directory}/dgpumini.vxd")
file(GLOB win98_icd_boundary_inputs CONFIGURE_DEPENDS "${DREAMGPU_VMDISP_ICD_SOURCE}/*.c"
  "${DREAMGPU_VMDISP_ICD_SOURCE}/*.h" "${DREAMGPU_VMDISP_ICD_SOURCE}/*.asm")
add_custom_command(OUTPUT ${icd_driver_outputs}
  COMMAND "${CMAKE_COMMAND}" -E env "WATCOM=${DREAMGPU_WATCOM_ROOT}"
    "PATH=${DREAMGPU_WATCOM_ROOT}/binl64:$ENV{PATH}"
    "INCLUDE=${DREAMGPU_WATCOM_ROOT}/h:${DREAMGPU_WATCOM_ROOT}/h/win"
    "${DREAMGPU_WATCOM_ROOT}/binl64/wmake" -a -h -f makefile.dreamgpu dgpumini.drv
    "FIXLINK_EXE=./fixlink-tool"
    "FIXLINK_CC=gcc -include strings.h -Dstricmp=strcasecmp fixlink/fixlink.c -o fixlink-tool"
    "CFLAGS=-q -wx -s -zu -zls -6 -fp6 -fo=.obj -fi=stdint.h"
    "CFLAGS32=-q -wx -s -zls -mf -DVXD32 -fpi87 -ei -oeatxhn -6s -fp6 -fo=.obj"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${icd_driver_directory}"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different dgpumini.drv "${icd_driver_directory}"
  # The VxD is deliberately the exact production artifact, not another variant.
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${driver_directory}/dgpumini.vxd" "${icd_driver_directory}"
  WORKING_DIRECTORY "${DREAMGPU_VMDISP_ICD_SOURCE}"
  DEPENDS dreamgpu-win98-driver ${win98_icd_objects} ${win98_icd_boundary_inputs}
    "${DREAMGPU_VMDISP_ICD_SOURCE}/makefile.dreamgpu"
  VERBATIM)
add_custom_target(dreamgpu-win98-icd-driver DEPENDS ${icd_driver_outputs})
install(FILES ${icd_driver_outputs} DESTINATION diagnostics/icd/drivers/win98 COMPONENT win98)
