glslang = find_program('glslangValidator', required : false)

solid_color_vert_spv = custom_target('solid_color_vert_spv',
  input  : 'shaders/solid_color.vert',
  output : 'solid_color_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_solid_color_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

solid_color_frag_spv = custom_target('solid_color_frag_spv',
  input  : 'shaders/solid_color.frag',
  output : 'solid_color_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_solid_color_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_vert_spv = custom_target('image_vert_spv',
  input  : 'shaders/image.vert',
  output : 'image_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_image_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_frag_spv = custom_target('image_frag_spv',
  input  : 'shaders/image.frag',
  output : 'image_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_image_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

text_frag_spv = custom_target('text_frag_spv',
  input  : 'shaders/text.frag',
  output : 'text_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_text_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

gradient_vert_spv = custom_target('gradient_vert_spv',
  input  : 'shaders/gradient.vert',
  output : 'gradient_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_gradient_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

gradient_frag_spv = custom_target('gradient_frag_spv',
  input  : 'shaders/gradient.frag',
  output : 'gradient_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_gradient_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

stencil_frag_spv = custom_target('stencil_frag_spv',
  input  : 'shaders/stencil.frag',
  output : 'stencil_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_stencil_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

blur_frag_spv = custom_target('blur_frag_spv',
  input  : 'shaders/blur.frag',
  output : 'blur_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_blur_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

fringe_vert_spv = custom_target('fringe_vert_spv',
  input  : 'shaders/fringe.vert',
  output : 'fringe_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_fringe_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

fringe_frag_spv = custom_target('fringe_frag_spv',
  input  : 'shaders/fringe.frag',
  output : 'fringe_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'flux_fringe_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

spv_targets = [
  solid_color_vert_spv, solid_color_frag_spv,
  image_vert_spv,       image_frag_spv,
  text_frag_spv,
  gradient_vert_spv,    gradient_frag_spv,
  stencil_frag_spv,     blur_frag_spv,
  fringe_vert_spv,      fringe_frag_spv,
]

# ---- New layered architecture sources ----
# Math layer:    vectors, matrices, arena allocator
# Geometry layer: path construction, bezier flattening, tessellation, strokes
# State layer:   canvas recording, display lists, transform stack
# RHI:           Render Hardware Interface abstraction + backends
# Engine:        backend-agnostic op execution
# Surface:       lifecycle + present

flux_layered_sources = files(
  'math/matrix.c',
  'math/color.c',
  'math/arena.c',

  'geometry/path.c',
  'geometry/tess.c',
  'geometry/stroker.c',

  'state/canvas.c',
  'state/paint.c',
  'state/gradient.c',

  'resource/context.c',
  'resource/image.c',
  'resource/log_helper.c',
  'resource/text.c',

  'rhi/software/software_rhi.c',

  'engine.c',
  'surface.c',
)

# Vulkan RHI backend (compiled when Vulkan is available)
flux_vk_sources = files(
  'resource/vulkan_device.c',
  'rhi/vulkan/vulkan_rhi.c',
  'rhi/vulkan/vk_swapchain.c',
  'rhi/vulkan/vk_pipeline.c',
  'rhi/vulkan/vk_texture.c',
  'rhi/vulkan/vk_draw.c',
)

if vulkan_dep.found() and glslang.found()
  flux_sources = flux_layered_sources + flux_vk_sources + spv_targets
  flux_deps = [vulkan_dep, threads_dep, m_dep]
else
  flux_sources = flux_layered_sources
  flux_deps = [threads_dep, m_dep]
  add_project_arguments('-DFLUX_NO_VULKAN=1', language : 'c')
endif

flux_c_args = ['-DFLUX_BUILDING=1']

flux_lib = library('flux',
  flux_sources,
  include_directories : flux_inc,
  c_args              : flux_c_args,
  dependencies        : flux_deps,
  gnu_symbol_visibility : 'hidden',
  install             : true,
)

flux_dep = declare_dependency(
  link_with           : flux_lib,
  include_directories : flux_inc,
  dependencies        : flux_deps,
)

install_headers('../include/flux/flux.h',            subdir : 'flux')
install_headers('../include/flux/flux_vulkan.h',     subdir : 'flux')
install_headers('../include/flux/flux_log_helper.h', subdir : 'flux')
