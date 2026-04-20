test_foundation = executable('test_foundation',
  'test_foundation.c',
  include_directories : vgfx_inc,
  link_with           : vgfx_lib,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
)

test_path_flatten = executable('test_path_flatten',
  'test_path_flatten.c',
  '../src/path.c',
  include_directories : vgfx_inc,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
)

test_tess = executable('test_tess',
  'test_tess.c',
  '../src/tess.c',
  include_directories : vgfx_inc,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
)

test_stroker = executable('test_stroker',
  'test_stroker.c',
  '../src/stroker.c',
  include_directories : vgfx_inc,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
)

test_transforms = executable('test_transforms',
  'test_transforms.c',
  '../src/path.c',
  include_directories : vgfx_inc,
  dependencies        : [vulkan_dep, threads_dep, m_dep, ft_dep, hb_dep] + wayland_deps,
)

test('foundation', test_foundation, suite : 'unit')
test('path_flatten', test_path_flatten, suite : 'unit')
test('tess', test_tess, suite : 'unit')
test('stroker', test_stroker, suite : 'unit')
test('transforms', test_transforms, suite : 'unit')
