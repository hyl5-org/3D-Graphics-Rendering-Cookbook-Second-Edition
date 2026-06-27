cmake_minimum_required(VERSION 3.19)

project(vrgraphics CXX C)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(BUILD_SHARED_LIBS OFF)

# TODO(luhanyang): Enable this in a dedicated OMM integration change.
# option(COOKBOOK_WITH_RTX_OMM "Build NVIDIA RTX Opacity Micro-Map SDK integration" OFF)
# set(COOKBOOK_RTX_OMM_ROOT "${COOKBOOK_ROOT_DIR}/deps/src/OMM" CACHE PATH "Path to NVIDIA RTX OMM SDK")

set(COOKBOOK_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(COOKBOOK_DEPS_CACHE_FILE "${COOKBOOK_ROOT_DIR}/deps/.bootstrap.json")

if($ENV{CLION_IDE})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${COOKBOOK_ROOT_DIR})
endif()

set(ENV{LVK_WITH_SLANG} "OFF")

if(NOT ANDROID)
  find_package(Python3 COMPONENTS Interpreter)

  execute_process(COMMAND ${Python3_EXECUTABLE} deploy_deps.py WORKING_DIRECTORY ${COOKBOOK_ROOT_DIR} COMMAND_ERROR_IS_FATAL ANY)
  execute_process(COMMAND ${Python3_EXECUTABLE} deploy_deps.py WORKING_DIRECTORY ${COOKBOOK_ROOT_DIR}/deps/src/lightweightvk COMMAND_ERROR_IS_FATAL ANY)

  add_custom_command(COMMAND ${Python3_EXECUTABLE} deploy_deps.py OUTPUT ${COOKBOOK_DEPS_CACHE_FILE}
                     DEPENDS "${COOKBOOK_ROOT_DIR}/deps/bootstrap.json" WORKING_DIRECTORY ${COOKBOOK_ROOT_DIR})
  add_custom_target(Dependencies DEPENDS ${COOKBOOK_DEPS_CACHE_FILE})
else()
  add_custom_target(Dependencies)
endif()
set_property(TARGET Dependencies PROPERTY FOLDER "third-party")

if(NOT ANDROID AND NOT EXISTS ${COOKBOOK_DEPS_CACHE_FILE})
  message(FATAL_ERROR "Cannot bootstrap dependencies.")
endif()

# cmake-format: off
add_subdirectory(deps/src/lightweightvk/third-party/deps/src/ldrutils)
set(LIBKTX_VERSION_READ_ONLY    OFF CACHE BOOL "")
set(LIBKTX_VERSION_FULL          ON CACHE BOOL "")
set(LIBKTX_FEATURE_KTX1          ON CACHE BOOL "")
set(LIBKTX_FEATURE_KTX2          ON CACHE BOOL "")
set(LIBKTX_FEATURE_VK_UPLOAD    OFF CACHE BOOL "")
set(LIBKTX_FEATURE_GL_UPLOAD    OFF CACHE BOOL "")
set(LIBKTX_FEATURE_ETC_UNPACK    ON CACHE BOOL "")
# cmake-format: on
add_subdirectory(deps/src/lightweightvk/third-party/deps/src/ktx-software/lib)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(basisu_encoder PUBLIC "-Wno-deprecated-anon-enum-enum-conversion")
endif()
set(LVK_WITH_SAMPLES   OFF CACHE BOOL "")
if(ANDROID)
  set(LVK_WITH_GLFW      OFF CACHE BOOL "" FORCE)
  set(LVK_WITH_TRACY     ON CACHE BOOL "" FORCE)
  set(LVK_WITH_TRACY_GPU OFF CACHE BOOL "" FORCE)
else()
  set(LVK_WITH_TRACY     ON  CACHE BOOL "")
  set(LVK_WITH_TRACY_GPU ON  CACHE BOOL "")
endif()
set(LVK_WITH_SLANG     OFF CACHE BOOL "")
add_subdirectory(deps/src/lightweightvk)
lvk_set_folder(ktx "third-party/ktx-software")
lvk_set_folder(ktx_version "third-party/ktx-software")
lvk_set_folder(basisu_encoder "third-party/ktx-software")
if(TARGET astcenc-avx2-static)
  lvk_set_folder(astcenc-avx2-static "third-party/ktx-software")
endif()
# cmake-format: on

set(ASSIMP_NO_EXPORT ON CACHE BOOL "")
set(ASSIMP_BUILD_DRACO OFF CACHE BOOL "")
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "")
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "")
set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "")
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "")
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "")
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "")
if(APPLE)
  # fix ZLib issues with new Clang
  set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "")
else()
  set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "")
endif()

add_subdirectory(deps/src/assimp)
if(APPLE)
  target_compile_options(assimp PRIVATE -Wno-deprecated-declarations)
endif()
if(UNIX)
  target_compile_options(assimp PRIVATE $<$<COMPILE_LANGUAGE:C,OBJC>:-Wno-deprecated-non-prototype>)
endif()

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "")
set(MESHOPT_BUILD_TOOLS OFF CACHE BOOL "")
set(MESHOPT_BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(deps/src/lightweightvk/third-party/deps/src/meshoptimizer)

if(WIN32)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_definitions(-DGLM_ENABLE_EXPERIMENTAL)

include_directories(.)
include_directories(deps/src)
include_directories(deps/src/lightweightvk/third-party/deps/src/taskflow)
include_directories(deps/src/lightweightvk/third-party/deps/src/glm)
include_directories(deps/src/lightweightvk/third-party/deps/src/imgui)
include_directories(deps/src/lightweightvk/third-party/deps/src/glslang)

add_subdirectory(deps/cmake/ImGuizmo)

# TODO(luhanyang): OMM runtime integration needs LVK VK_EXT_opacity_micromap support first.
# if(COOKBOOK_WITH_RTX_OMM)
#   if(ANDROID)
#     message(WARNING "COOKBOOK_WITH_RTX_OMM is disabled on Android for now.")
#     set(COOKBOOK_WITH_RTX_OMM OFF CACHE BOOL "" FORCE)
#   elseif(EXISTS "${COOKBOOK_RTX_OMM_ROOT}/CMakeLists.txt")
#     set(OMM_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
#     set(OMM_BUILD_VIEWER OFF CACHE BOOL "" FORCE)
#     set(OMM_BUILD_OMM_GPU_NVRHI OFF CACHE BOOL "" FORCE)
#     set(OMM_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
#     set(OMM_LIB_INSTALL OFF CACHE BOOL "" FORCE)
#     set(OMM_ENABLE_PRECOMPILED_SHADERS_DXIL OFF CACHE BOOL "" FORCE)
#     set(OMM_ENABLE_PRECOMPILED_SHADERS_SPIRV OFF CACHE BOOL "" FORCE)
#     set(OMM_PROJECT_FOLDER "third-party/OMM" CACHE STRING "" FORCE)
#     add_subdirectory("${COOKBOOK_RTX_OMM_ROOT}" "${CMAKE_BINARY_DIR}/deps/OMM")
#   else()
#     message(WARNING "COOKBOOK_WITH_RTX_OMM requested, but COOKBOOK_RTX_OMM_ROOT does not point to an OMM SDK checkout.")
#     set(COOKBOOK_WITH_RTX_OMM OFF CACHE BOOL "" FORCE)
#   endif()
# endif()

set_property(TARGET assimp          PROPERTY FOLDER "third-party")
set_property(TARGET LUtils          PROPERTY FOLDER "third-party")
set_property(TARGET meshoptimizer   PROPERTY FOLDER "third-party")
if(WIN32)
  if(TARGET zlibstatic)
    set_property(TARGET zlibstatic  PROPERTY FOLDER "third-party")
  endif()
  if(TARGET UpdateAssimpLibsDebugSymbolsAndDLLs)
    set_property(TARGET UpdateAssimpLibsDebugSymbolsAndDLLs PROPERTY FOLDER "third-party")
  endif()
endif()
set_property(TARGET uninstall       PROPERTY FOLDER "third-party")

# shared utils code
add_subdirectory(shared)
add_subdirectory(data)

add_dependencies(SharedUtils Dependencies)

# disable warnings in third-party code
if(MSVC)
	target_compile_options(assimp PRIVATE /wd4267)
	target_compile_options(SPIRV  PRIVATE /wd4267)
endif()

# This fork maintains only the final Chapter 11 demo as an app target.
add_subdirectory(Renderer)
