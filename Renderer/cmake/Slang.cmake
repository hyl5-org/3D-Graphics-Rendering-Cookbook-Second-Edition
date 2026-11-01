# slangc runs on the build host, including when cross-compiling for Android.
find_program(RENDERER_SLANGC NAMES slangc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    NO_CMAKE_FIND_ROOT_PATH REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

file(GLOB RENDERER_SLANG_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../shaders/*.slang")
set(RENDERER_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(
    OUTPUT "${RENDERER_SHADER_OUTPUT_DIR}/RendererShaders.h"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/../tools/CompileShaders.py"
        --compiler "${RENDERER_SLANGC}"
        --source-dir "${CMAKE_CURRENT_LIST_DIR}/../shaders"
        --output-dir "${RENDERER_SHADER_OUTPUT_DIR}"
    DEPENDS ${RENDERER_SLANG_SOURCES} "${CMAKE_CURRENT_LIST_DIR}/../shaders/shadow_occlusion_culling_config.h"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/CompileShaders.py" "${RENDERER_SLANGC}"
    COMMENT "Compiling Renderer Slang shaders"
    VERBATIM)
add_custom_target(RendererShaders DEPENDS "${RENDERER_SHADER_OUTPUT_DIR}/RendererShaders.h")
set_property(TARGET RendererShaders PROPERTY FOLDER "shaders")
