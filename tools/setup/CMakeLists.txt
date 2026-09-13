# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.25)
project(DreamGPUSetup LANGUAGES C CXX RC)
get_filename_component(DREAMGPU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(DREAMGPU_GUEST "${DREAMGPU_ROOT}/guest")
include("${DREAMGPU_ROOT}/support/guest/cmake/setup.cmake")
