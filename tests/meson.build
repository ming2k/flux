tests = [
  'test_version',
  'test_matrix',
  'test_color',
  'test_path',
  'test_paint',
  'test_gradient',
  'test_image',
  'test_glyph_run',
  'test_canvas_transform',
  'test_offscreen',
  'test_allocator',
  'test_null_safety',
  'test_api_surface',
]

foreach name : tests
  exe = executable(name, name + '.c', dependencies : [flux_dep])
  test(name.substring(5), exe, suite : 'unit')
endforeach
