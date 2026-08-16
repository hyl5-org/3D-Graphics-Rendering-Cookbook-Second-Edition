cmake_minimum_required(VERSION 3.19)

project(PTRenderer)

include(../CMake/CommonMacros.txt)

set(APP_NAME PTRenderer)
set(ANDROID_TEXTURE_CACHE_TOOL ${APP_NAME}_AndroidTextureCache)

SETUP_APP(${APP_NAME} "Chapter 11")
target_link_libraries(${APP_NAME} PRIVATE SharedUtils assimp meshoptimizer)
# TODO(luhanyang): Link OMM SDK once the runtime micromap backend is implemented.
# if(COOKBOOK_WITH_RTX_OMM AND TARGET omm-lib)
#   target_link_libraries(${APP_NAME} PRIVATE omm-lib)
#   target_compile_definitions(${APP_NAME} PRIVATE COOKBOOK_WITH_RTX_OMM=1)
# endif()
file(GLOB_RECURSE FINAL_DEMO_SHADER_FILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     shaders/*.vert shaders/*.frag shaders/*.geom shaders/*.sp shaders/*.comp shaders/*.tesc shaders/*.tese)
target_sources(${APP_NAME} PRIVATE ${FINAL_DEMO_SHADER_FILES})


