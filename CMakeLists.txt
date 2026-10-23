# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)
project(AND_Awareness VERSION 1.0.0 LANGUAGES CXX)

# SE + AE (flatrim) only; no hooks, so the trampoline patch-safety fetch is unnecessary.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/commonlibsse-ng commonlib)

# A plain source add_subdirectory does not define the helper.
if(NOT COMMAND add_commonlibsse_plugin)
    include(lib/commonlibsse-ng/cmake/CommonLibSSE.cmake)
endif()

add_commonlibsse_plugin(${PROJECT_NAME} AUTHOR "nosimo" SOURCES src/plugin.cpp)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_compile_options(${PROJECT_NAME} PRIVATE /Zc:preprocessor /permissive- /EHsc /W4)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
# SkyrimNet C++ API headers, copied from the SkyrimNet devkit (CppAPI/).
target_include_directories(${PROJECT_NAME} PRIVATE include)
