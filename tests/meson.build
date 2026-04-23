test_foundation = executable('test_foundation',
  'test_foundation.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_path_flatten = executable('test_path_flatten',
  'test_path_flatten.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_arc = executable('test_arc',
  'test_arc.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_tess = executable('test_tess',
  'test_tess.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_stroker = executable('test_stroker',
  'test_stroker.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_transforms = executable('test_transforms',
  'test_transforms.c',
  dependencies : [flux_dep] + wayland_deps,
)

test_offscreen = executable('test_offscreen',
  'test_offscreen.c',
  dependencies : [flux_dep],
)

test('foundation', test_foundation, suite : 'unit')
test('path_flatten', test_path_flatten, suite : 'unit')
test('arc', test_arc, suite : 'unit')
test('tess', test_tess, suite : 'unit')
test('stroker', test_stroker, suite : 'unit')
test('transforms', test_transforms, suite : 'unit')
test('offscreen', test_offscreen, suite : 'unit')
