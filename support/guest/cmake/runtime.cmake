# SPDX-License-Identifier: GPL-2.0-or-later
function(dreamgpu_cpp target)
  target_compile_features(${target} PRIVATE cxx_std_23)
  target_compile_options(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics;-fno-use-cxa-atexit>")
endfunction()

function(dreamgpu_freestanding target)
  dreamgpu_cpp(${target})
  target_compile_options(${target} PRIVATE -march=pentium3 -mno-sse -mno-sse2
    -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra)
  target_include_directories(${target} PRIVATE
    "${DREAMGPU_GUEST}/include" "${DREAMGPU_GUEST}/nt/include")
endfunction()

function(dreamgpu_user target)
  dreamgpu_freestanding(${target})
  target_compile_options(${target} PRIVATE -O2 -Werror)
  target_compile_definitions(${target} PRIVATE _WIN32_WINNT=0x0400 WINVER=0x0400)
  target_link_options(${target} PRIVATE -nostdlib
    -Wl,--subsystem,windows:4.0,--no-insert-timestamp)
  set_target_properties(${target} PROPERTIES PREFIX "" LINKER_LANGUAGE C)
endfunction()
