cmake_minimum_required(VERSION 3.20)
project(flux VERSION 0.2.3 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(FLUX_VULKAN vulkan)
find_program(GLSLANG_VALIDATOR glslangValidator)

set(FLUX_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders")
set(FLUX_SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/src")

function(flux_shader output input symbol)
    add_custom_command(
        OUTPUT "${FLUX_SHADER_BIN_DIR}/${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${FLUX_SHADER_BIN_DIR}"
        COMMAND "${GLSLANG_VALIDATOR}"
                -V --target-env vulkan1.2 --quiet
                --vn "${symbol}"
                "${FLUX_SHADER_DIR}/${input}"
                -o "${FLUX_SHADER_BIN_DIR}/${output}"
        DEPENDS "${FLUX_SHADER_DIR}/${input}"
        VERBATIM
    )
endfunction()

set(FLUX_SHADER_INCS)

if(FLUX_VULKAN_FOUND AND GLSLANG_VALIDATOR)
    flux_shader(solid_color_vert_spv.inc solid_color.vert flux_solid_color_vert_spv)
    flux_shader(solid_color_frag_spv.inc solid_color.frag flux_solid_color_frag_spv)
    flux_shader(image_vert_spv.inc       image.vert       flux_image_vert_spv)
    flux_shader(image_frag_spv.inc       image.frag       flux_image_frag_spv)
    flux_shader(text_frag_spv.inc        text.frag        flux_text_frag_spv)
    flux_shader(gradient_vert_spv.inc    gradient.vert    flux_gradient_vert_spv)
    flux_shader(gradient_frag_spv.inc    gradient.frag    flux_gradient_frag_spv)
    flux_shader(stencil_frag_spv.inc     stencil.frag     flux_stencil_frag_spv)
    flux_shader(blur_frag_spv.inc        blur.frag        flux_blur_frag_spv)
    flux_shader(fringe_vert_spv.inc      fringe.vert      flux_fringe_vert_spv)
    flux_shader(fringe_frag_spv.inc      fringe.frag      flux_fringe_frag_spv)

    set(FLUX_SHADER_INCS
        "${FLUX_SHADER_BIN_DIR}/solid_color_vert_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/solid_color_frag_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/image_vert_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/image_frag_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/text_frag_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/gradient_vert_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/gradient_frag_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/stencil_frag_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/blur_frag_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/fringe_vert_spv.inc"
        "${FLUX_SHADER_BIN_DIR}/fringe_frag_spv.inc"
    )
endif()

set(FLUX_LAYERED_SOURCES
    src/math/matrix.c
    src/math/color.c
    src/math/arena.c

    src/geometry/path.c
    src/geometry/tess.c
    src/geometry/stroker.c

    src/state/canvas.c
    src/state/paint.c
    src/state/gradient.c

    src/resource/context.c
    src/resource/image.c
    src/resource/log_helper.c
    src/resource/text.c

    src/rhi/software/software_rhi.c

    src/engine.c
    src/surface.c
)

set(FLUX_COMPILE_DEFS _GNU_SOURCE FLUX_BUILDING=1)
set(FLUX_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/src"
)

if(FLUX_VULKAN_FOUND AND GLSLANG_VALIDATOR)
    list(APPEND FLUX_LAYERED_SOURCES
        src/resource/vulkan_device.c
        src/rhi/vulkan/vulkan_rhi.c
        src/rhi/vulkan/vk_swapchain.c
        src/rhi/vulkan/vk_pipeline.c
        src/rhi/vulkan/vk_texture.c
        src/rhi/vulkan/vk_draw.c
    )
    list(APPEND FLUX_COMPILE_DEFS FLUX_VULKAN=1)
else()
    list(APPEND FLUX_COMPILE_DEFS FLUX_NO_VULKAN=1)
endif()

add_library(flux STATIC ${FLUX_LAYERED_SOURCES} ${FLUX_SHADER_INCS})

target_compile_definitions(flux PRIVATE ${FLUX_COMPILE_DEFS})
target_include_directories(flux
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_BINARY_DIR}/src"
)

if(FLUX_VULKAN_FOUND AND GLSLANG_VALIDATOR)
    target_include_directories(flux SYSTEM PRIVATE ${FLUX_VULKAN_INCLUDE_DIRS})
    target_link_libraries(flux PUBLIC ${FLUX_VULKAN_LIBRARIES})
endif()

target_link_libraries(flux PUBLIC m pthread)

set_target_properties(flux PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_library(flux::flux ALIAS flux)

install(TARGETS flux
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(DIRECTORY include/flux DESTINATION include)
