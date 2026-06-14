cmake_minimum_required(VERSION 3.19)

project(Renderer)

include(../CMake/CommonMacros.txt)

set(APP_NAME Renderer)
set(ANDROID_TEXTURE_CACHE_TOOL ${APP_NAME}_AndroidTextureCache)

if(ANDROID)
  if(DEFINED ENV{ANDROID_NDK})
    cmake_path(SET NDK_PATH "$ENV{ANDROID_NDK}")
  else()
    cmake_path(SET NDK_PATH "$ENV{NDK_ROOT}")
  endif()

  file(GLOB_RECURSE SRC_FILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} src/*.c??)
  file(GLOB_RECURSE HEADER_FILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} src/*.h)
  file(GLOB_RECURSE SHADER_FILES LIST_DIRECTORIES false RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
      src/*.vert src/*.frag src/*.geom src/*.sp src/*.comp src/*.tesc src/*.tese
      shaders/*.vert shaders/*.frag shaders/*.geom shaders/*.sp shaders/*.comp shaders/*.tesc shaders/*.tese)

  set(ANDROID_NATIVE_APP_GLUE_SRC "${CMAKE_CURRENT_BINARY_DIR}/android_native_app_glue.c")
  configure_file("${NDK_PATH}/sources/android/native_app_glue/android_native_app_glue.c"
                 "${ANDROID_NATIVE_APP_GLUE_SRC}" COPYONLY)

  add_library(lvk_android_native_${APP_NAME} SHARED
      ${SRC_FILES}
      ${HEADER_FILES}
      ${SHADER_FILES}
      "${ANDROID_NATIVE_APP_GLUE_SRC}")
  target_include_directories(lvk_android_native_${APP_NAME} PRIVATE
      src
      "${NDK_PATH}/sources/android/native_app_glue")
  target_link_libraries(lvk_android_native_${APP_NAME} PRIVATE SharedUtils assimp meshoptimizer android log)
  set_target_properties(lvk_android_native_${APP_NAME} PROPERTIES
      DEBUG_POSTFIX ""
      RELWITHDEBINFO_POSTFIX ""
      MINSIZEREL_POSTFIX "")
  set_property(TARGET lvk_android_native_${APP_NAME} PROPERTY CXX_STANDARD 20)
  set_property(TARGET lvk_android_native_${APP_NAME} PROPERTY CXX_STANDARD_REQUIRED ON)
else()
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

  if((DEFINED ENV{ANDROID_NDK}) OR (DEFINED ENV{NDK_ROOT}))
    set(ANDROID_PROJECT_DIR "${CMAKE_BINARY_DIR}/android/${APP_NAME}")
    set(LVK_ANDROID_TEMPLATE_DIR "${COOKBOOK_ROOT_DIR}/deps/src/lightweightvk/android")

    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/build.gradle"
         DESTINATION "${ANDROID_PROJECT_DIR}")
    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/gradle.properties"
         DESTINATION "${ANDROID_PROJECT_DIR}")
    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/gradlew"
         DESTINATION "${ANDROID_PROJECT_DIR}")
    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/gradlew.bat"
         DESTINATION "${ANDROID_PROJECT_DIR}")
    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/gradle/wrapper/gradle-wrapper.jar"
         DESTINATION "${ANDROID_PROJECT_DIR}/gradle/wrapper")
    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/gradle/wrapper/gradle-wrapper.properties"
         DESTINATION "${ANDROID_PROJECT_DIR}/gradle/wrapper")
    file(COPY "${LVK_ANDROID_TEMPLATE_DIR}/lvklib"
         DESTINATION "${CMAKE_BINARY_DIR}/android")

    set(ABI_STRING "")
    foreach(str ${LVK_ANDROID_ABI})
      if(NOT "${ABI_STRING}" STREQUAL "")
        set(ABI_STRING "${ABI_STRING}, ")
      endif()
      set(ABI_STRING "${ABI_STRING}'${str}'")
    endforeach()
    set(ANDROID_ABI_FILTERS ${ABI_STRING})
    set(COOKBOOK_ANDROID_CMAKELISTS "${COOKBOOK_ROOT_DIR}/CMakeLists.txt")
    set(COOKBOOK_ANDROID_PYTHON3 "${Python3_EXECUTABLE}")

    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/android/settings.gradle.in"
                   "${ANDROID_PROJECT_DIR}/settings.gradle"
                   @ONLY)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/android/app/build.gradle.in"
                   "${ANDROID_PROJECT_DIR}/app/build.gradle"
                   @ONLY)
    configure_file("${LVK_ANDROID_TEMPLATE_DIR}/app/AndroidManifest.xml.in"
                   "${ANDROID_PROJECT_DIR}/app/src/main/AndroidManifest.xml")
    configure_file("${LVK_ANDROID_TEMPLATE_DIR}/app/MainActivity.java.in"
                   "${ANDROID_PROJECT_DIR}/app/src/main/java/org/lvk/samples/MainActivity.java")
  endif()

  add_executable(${ANDROID_TEXTURE_CACHE_TOOL} tools/GenerateAndroidTextureCache.cpp)
  target_include_directories(${ANDROID_TEXTURE_CACHE_TOOL} PRIVATE src)
  target_compile_definitions(${ANDROID_TEXTURE_CACHE_TOOL} PRIVATE COOKBOOK_ANDROID_TEXTURE=1 COOKBOOK_ANDROID_TEXTURE_CACHE=1)
  target_link_libraries(${ANDROID_TEXTURE_CACHE_TOOL} PRIVATE SharedUtils assimp meshoptimizer)
  set_property(TARGET ${ANDROID_TEXTURE_CACHE_TOOL} PROPERTY CXX_STANDARD 20)
  set_property(TARGET ${ANDROID_TEXTURE_CACHE_TOOL} PROPERTY CXX_STANDARD_REQUIRED ON)
  set_property(TARGET ${ANDROID_TEXTURE_CACHE_TOOL} PROPERTY FOLDER "tools")

  add_custom_target(${APP_NAME}_GenerateAndroidTextureCache
      COMMAND $<TARGET_FILE:${ANDROID_TEXTURE_CACHE_TOOL}>
      DEPENDS ${ANDROID_TEXTURE_CACHE_TOOL}
      WORKING_DIRECTORY ${COOKBOOK_ROOT_DIR}
      COMMENT "Generating Android ASTC Bistro texture cache")
  set_property(TARGET ${APP_NAME}_GenerateAndroidTextureCache PROPERTY FOLDER "tools")

endif()
