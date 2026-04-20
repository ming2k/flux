glslang = find_program('glslangValidator', required : true)

solid_color_vert_spv = custom_target('solid_color_vert_spv',
  input  : 'shaders/solid_color.vert',
  output : 'solid_color_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'vg_solid_color_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

solid_color_frag_spv = custom_target('solid_color_frag_spv',
  input  : 'shaders/solid_color.frag',
  output : 'solid_color_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'vg_solid_color_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_vert_spv = custom_target('image_vert_spv',
  input  : 'shaders/image.vert',
  output : 'image_vert_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'vg_image_vert_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

image_frag_spv = custom_target('image_frag_spv',
  input  : 'shaders/image.frag',
  output : 'image_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'vg_image_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

text_frag_spv = custom_target('text_frag_spv',
  input  : 'shaders/text.frag',
  output : 'text_frag_spv.inc',
  command: [glslang, '-V', '--target-env', 'vulkan1.2',
            '--quiet', '--vn', 'vg_text_frag_spv',
            '@INPUT@', '-o', '@OUTPUT@'],
)

vgfx_sources = files(
  'context.c',
  'canvas.c',
  'image.c',
  'path.c',
  'surface.c',
  'stroker.c',
  'tess.c',
  'text.c',
  'vk/device.c',
  'vk/swapchain.c',
  'vk/memory.c',
) + [
  solid_color_vert_spv,
  solid_color_frag_spv,
  image_vert_spv,
  image_frag_spv,
  text_frag_spv,
]

vgfx_c_args = [
  '-DVGFX_BUILDING=1',
  '-DVK_USE_PLATFORM_WAYLAND_KHR=1',
]

vgfx_lib = library('vgfx',
  vgfx_sources,
  include_directories : vgfx_inc,
  c_args              : vgfx_c_args,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
  gnu_symbol_visibility : 'hidden',
  install             : true,
)

vgfx_dep = declare_dependency(
  link_with           : vgfx_lib,
  include_directories : include_directories('../include'),
  dependencies        : [vulkan_dep],
)

install_headers('../include/vgfx/vgfx.h',         subdir : 'vgfx')
install_headers('../include/vgfx/vgfx_wayland.h', subdir : 'vgfx')
