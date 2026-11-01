cmake_minimum_required(VERSION 3.19)
project(NeuralTexture CXX)
include(../CMake/CommonMacros.txt)
SETUP_APP(NeuralTexture "Samples")

target_link_libraries(NeuralTexture PRIVATE SharedUtils)
find_program(NTC_SLANGC NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)

function(ntc_compile_shader name source entry decoder)
  set(output "${CMAKE_CURRENT_BINARY_DIR}/${name}.spv")
  set(flags -profile spirv_1_5)
  if(decoder STREQUAL "coop")
    set(flags -profile spirv_1_6 -capability spvCooperativeVectorNV -DNTC_COOP_VECTOR=1)
  elseif(decoder STREQUAL "fp16")
    set(flags -profile spirv_1_5 -DNTC_SCALAR_FP16=1)
  endif()
  add_custom_command(OUTPUT "${output}"
    COMMAND "${NTC_SLANGC}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${source}"
      -entry "${entry}" -target spirv ${flags} -matrix-layout-column-major
      -fvk-use-scalar-layout -Wno-39001 -Wno-41012 -o "${output}"
    DEPENDS "shaders/${source}" shaders/neural_texture_decode.slang "${NTC_SLANGC}" VERBATIM)
  target_sources(NeuralTexture PRIVATE "${output}")
  string(TOUPPER "${name}" name_upper)
  target_compile_definitions(NeuralTexture PRIVATE "NTC_${name_upper}_SPV=\"${output}\"")
endfunction()

ntc_compile_shader(vertex neural_texture.slang vertexMain fp32)
ntc_compile_shader(fragment neural_texture.slang fragmentMain fp32)
ntc_compile_shader(fragment_coop neural_texture.slang fragmentMain coop)
ntc_compile_shader(validate neural_texture_validate.slang main fp32)
ntc_compile_shader(validate_coop neural_texture_validate.slang main coop)

ntc_compile_shader(fragment_fp16 neural_texture.slang fragmentMain fp16)
ntc_compile_shader(validate_fp16 neural_texture_validate.slang main fp16)
