# SPDX-License-Identifier: GPL-2.0-or-later
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "16.1.1")
  message(FATAL_ERROR "Installer requires pinned MinGW GCC16.1.1")
endif()
if(NOT EXISTS "${DREAMGPU_SETUP_INPUT}/payload.rc" OR NOT EXISTS "${DREAMGPU_SETUP_INPUT}/icons.rc")
  message(FATAL_ERROR "Rust must validate and generate installer resources first")
endif()
include("${DREAMGPU_ROOT}/support/guest/cmake/runtime.cmake")
add_executable(dreamgpu-setup "${DREAMGPU_ROOT}/tools/setup/main.cpp"
  "${DREAMGPU_ROOT}/guest/nt/memory.cpp" "${DREAMGPU_SETUP_INPUT}/payload.rc"
  "${DREAMGPU_SETUP_INPUT}/icons.rc")
dreamgpu_user(dreamgpu-setup)
target_include_directories(dreamgpu-setup PRIVATE "${DREAMGPU_SETUP_INPUT}")
target_link_options(dreamgpu-setup PRIVATE -Wl,--entry,_WinMainCRTStartup@0,--fatal-warnings)
# MinGW also exports CM_* from its NT SetupAPI import library. Resolve those
# symbols from CfgMgr32 first: the Win98 SetupAPI DLL does not export them.
target_link_libraries(dreamgpu-setup PRIVATE kernel32 user32 cfgmgr32 setupapi advapi32 gcc)
set_target_properties(dreamgpu-setup PROPERTIES OUTPUT_NAME dreamgpu SUFFIX ".exe")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Fixed acceptance helper is built only against a concrete, audited installer.
if(DEFINED DREAMGPU_SETUP_VERIFY_HASH)
  string(LENGTH "${DREAMGPU_SETUP_VERIFY_HASH}" setup_verify_length)
  if(NOT setup_verify_length EQUAL 64 OR NOT DREAMGPU_SETUP_VERIFY_HASH MATCHES "^[0-9a-f]+$")
    message(FATAL_ERROR "Setup acceptance helper requires exact installer SHA256")
  endif()
  add_executable(dreamgpu-setup-verify "${DREAMGPU_ROOT}/tools/setup/verify.cpp"
    "${DREAMGPU_ROOT}/guest/nt/memory.cpp")
  dreamgpu_user(dreamgpu-setup-verify)
  target_compile_definitions(dreamgpu-setup-verify PRIVATE "DG_EXPECTED_INSTALLER_HASH=\"${DREAMGPU_SETUP_VERIFY_HASH}\"")
  target_link_options(dreamgpu-setup-verify PRIVATE -Wl,--entry,_WinMainCRTStartup@0,--fatal-warnings)
  target_link_libraries(dreamgpu-setup-verify PRIVATE kernel32 user32 advapi32 gcc)
  set_target_properties(dreamgpu-setup-verify PROPERTIES OUTPUT_NAME DGSETTST SUFFIX ".EXE")
endif()

if(DEFINED DREAMGPU_SETUP_VERIFY_HASH)
  foreach(driver_phase RANGE 0 2)
    if(driver_phase EQUAL 0)
      set(driver_helper DGDRVB)
    elseif(driver_phase EQUAL 1)
      set(driver_helper DGDRVC)
    else()
      set(driver_helper DGDRVR)
    endif()
    add_executable(dreamgpu-driver-${driver_phase} "${DREAMGPU_ROOT}/tools/setup/driver-verify.cpp"
      "${DREAMGPU_ROOT}/guest/nt/memory.cpp")
    dreamgpu_user(dreamgpu-driver-${driver_phase})
    target_compile_definitions(dreamgpu-driver-${driver_phase} PRIVATE
      "DG_EXPECTED_INSTALLER_HASH=\"${DREAMGPU_SETUP_VERIFY_HASH}\"" "DG_DRIVER_PHASE=${driver_phase}")
    target_link_options(dreamgpu-driver-${driver_phase} PRIVATE -Wl,--entry,_WinMainCRTStartup@0,--fatal-warnings)
    target_link_libraries(dreamgpu-driver-${driver_phase} PRIVATE kernel32 user32 gcc)
    set_target_properties(dreamgpu-driver-${driver_phase} PROPERTIES OUTPUT_NAME ${driver_helper} SUFFIX ".EXE")
  endforeach()
endif()

if(DEFINED DREAMGPU_SETUP_VERIFY_HASH)
  add_executable(dreamgpu-system-ui "${DREAMGPU_ROOT}/tools/setup/system-verify.cpp"
    "${DREAMGPU_ROOT}/guest/nt/memory.cpp")
  dreamgpu_user(dreamgpu-system-ui)
  target_compile_definitions(dreamgpu-system-ui PRIVATE DG_SYSTEM_UI DG_SYSTEM_PHASE=1
    "DG_EXPECTED_INSTALLER_HASH=\"${DREAMGPU_SETUP_VERIFY_HASH}\"")
  target_link_options(dreamgpu-system-ui PRIVATE -Wl,--entry,_WinMainCRTStartup@0,--fatal-warnings)
  target_link_libraries(dreamgpu-system-ui PRIVATE kernel32 user32 gcc)
  set_target_properties(dreamgpu-system-ui PROPERTIES OUTPUT_NAME DGSETUI SUFFIX ".EXE")
endif()

# Full-system fixture operations share the same audited installer identity.
if(DEFINED DREAMGPU_SETUP_VERIFY_HASH)
  set(system_helpers DGINST DGCONT DGUPGR DGUNDO DGREMOVE DGREPAIR DGRECOV)
  foreach(system_phase RANGE 0 6)
    list(GET system_helpers ${system_phase} system_helper)
    add_executable(dreamgpu-system-${system_phase} "${DREAMGPU_ROOT}/tools/setup/system-verify.cpp"
      "${DREAMGPU_ROOT}/guest/nt/memory.cpp")
    dreamgpu_user(dreamgpu-system-${system_phase})
    target_compile_definitions(dreamgpu-system-${system_phase} PRIVATE
      "DG_EXPECTED_INSTALLER_HASH=\"${DREAMGPU_SETUP_VERIFY_HASH}\"" "DG_SYSTEM_PHASE=${system_phase}")
    target_link_options(dreamgpu-system-${system_phase} PRIVATE -Wl,--entry,_WinMainCRTStartup@0,--fatal-warnings)
    target_link_libraries(dreamgpu-system-${system_phase} PRIVATE kernel32 user32 gcc)
    set_target_properties(dreamgpu-system-${system_phase} PROPERTIES OUTPUT_NAME ${system_helper} SUFFIX ".EXE")
  endforeach()
endif()
