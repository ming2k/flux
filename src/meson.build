glslang = find_program('glslangValidator', required : true)

solid_color_vert_spv = custom_target('solid_color_vert_spv',
  input  : 'shaders/solid_color.vert',
  output : 'solid_color_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_solid_color_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

solid_color_frag_spv = custom_target('solid_color_frag_spv',
  input  : 'shaders/solid_color.frag',
  output : 'solid_color_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_solid_color_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_vert_spv = custom_target('image_vert_spv',
  input  : 'shaders/image.vert',
  output : 'image_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_image_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_frag_spv = custom_target('image_frag_spv',
  input  : 'shaders/image.frag',
  output : 'image_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_image_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

text_frag_spv = custom_target('text_frag_spv',
  input  : 'shaders/text.frag',
  output : 'text_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_text_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

gradient_vert_spv = custom_target('gradient_vert_spv',
  input  : 'shaders/gradient.vert',
  output : 'gradient_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_gradient_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

gradient_frag_spv = custom_target('gradient_frag_spv',
  input  : 'shaders/gradient.frag',
  output : 'gradient_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_gradient_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

stencil_frag_spv = custom_target('stencil_frag_spv',
  input  : 'shaders/stencil.frag',
  output : 'stencil_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_stencil_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

blur_frag_spv = custom_target('blur_frag_spv',
  input  : 'shaders/blur.frag',
  output : 'blur_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'fx_blur_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

spv_targets = [
  solid_color_vert_spv, solid_color_frag_spv,
  image_vert_spv,       image_frag_spv,
  text_frag_spv,
  gradient_vert_spv,    gradient_frag_spv,
  stencil_frag_spv,     blur_frag_spv,
]

# ---- New layered architecture sources ----
# Math layer:    vectors, matrices, arena allocator
# Geometry layer: path construction, bezier flattening, tessellation, strokes
# State layer:   canvas recording, display lists, transform stack
# Renderer:      backend abstraction (vtable) + software + vulkan
# Engine:        backend-agnostic op execution
# Surface:       lifecycle + present

flux_layered_sources = files(
  'math/matrix.c',
  'math/arena.c',

  'geometry/path.c',
  'geometry/tess.c',
  'geometry/stroker.c',

  'state/canvas.c',

  'resource/context.c',
  'resource/image.c',
  'resource/text.c',

  'renderer/software.c',

  'engine.c',
  'surface.c',
)

# Vulkan backend (compiled when Vulkan hardware rendering is needed)
# The device/swapchain/memory/upload files need porting to the vtable
# interface. For now keep them out of the build — the stub vulkan_renderer
# compiles and proves the architecture.
flux_vk_sources = files(
  'renderer/vulkan/vulkan_renderer.c',
)

flux_sources = flux_layered_sources + flux_vk_sources + spv_targets

flux_c_args = ['-DFX_BUILDING=1']

flux_lib = library('flux',
  flux_sources,
  include_directories : flux_inc,
  c_args              : flux_c_args,
  dependencies        : [vulkan_dep, threads_dep, m_dep],
  gnu_symbol_visibility : 'hidden',
  install             : true,
)

flux_dep = declare_dependency(
  link_with           : flux_lib,
  include_directories : flux_inc,
  dependencies        : [vulkan_dep],
)

install_headers('../include/flux/flux.h',         subdir : 'flux')
install_headers('../include/flux/flux_vulkan.h',  subdir : 'flux')
