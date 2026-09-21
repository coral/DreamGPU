# SPDX-License-Identifier: GPL-2.0-or-later
set(frontend_sources frontend query texture compatibility readback arrays secondary fixed provider transport transport9-window)
set(frontend_units "${DREAMGPU_GUEST}/nt/memory.cpp")
foreach(unit IN LISTS frontend_sources)
  list(APPEND frontend_units "${DREAMGPU_GUEST}/opengl/${unit}.cpp")
endforeach()
# scalar.inc and frontend.def are reviewed, committed generated inputs. The
# compiler never runs a generator or modifies the source checkout.
add_library(dgpugl SHARED ${frontend_units} "${DREAMGPU_GUEST}/opengl/frontend.def")
dreamgpu_user(dgpugl)
target_link_options(dgpugl PRIVATE -Wl,--entry,_DllMain@12)
target_link_libraries(dgpugl PRIVATE kernel32 user32 gdi32 gcc)
set_target_properties(dgpugl PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/application")
install(TARGETS dgpugl RUNTIME DESTINATION application COMPONENT ${DREAMGPU_GUEST_OS})

# System ICD and the app-local frontend share the same implementation.
add_library(dgpuicd SHARED ${frontend_units} "${DREAMGPU_GUEST}/opengl/icd.cpp"
  "${DREAMGPU_GUEST}/opengl/icd.def")
dreamgpu_user(dgpuicd)
target_link_options(dgpuicd PRIVATE -Wl,--entry,_DllMain@12)
target_link_libraries(dgpuicd PRIVATE kernel32 user32 gdi32 gcc)
set_target_properties(dgpuicd PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/application")
install(TARGETS dgpuicd RUNTIME DESTINATION application COMPONENT ${DREAMGPU_GUEST_OS})
install(FILES "${DREAMGPU_GUEST}/opengl/icd-coverage.json"
  DESTINATION licenses/opengl COMPONENT ${DREAMGPU_GUEST_OS})
