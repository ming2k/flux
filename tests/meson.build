unit_tests = [
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

foreach name : unit_tests
  exe = executable(name, name + '.c', dependencies : [flux_dep])
  test(name.substring(5), exe, suite : 'unit')
endforeach

# ------------------------------------------------------------------
# Golden-image regression tests
# ------------------------------------------------------------------
golden_exe = executable('test_golden', 'test_golden.c',
  dependencies : [flux_dep])

test('golden', golden_exe,
  suite     : 'golden',
  timeout   : 30,
  env       : {
    'FLUX_GOLDEN_DIR' : meson.current_source_dir() / 'golden',
  },
)

# ------------------------------------------------------------------
# Vulkan smoke test (driver-dependent, may skip)
# ------------------------------------------------------------------
if vulkan_dep.found()
  vulkan_smoke_exe = executable('test_vulkan_smoke', 'test_vulkan_smoke.c',
    dependencies : [flux_dep])
  test('vulkan_smoke', vulkan_smoke_exe,
    suite   : 'integration',
    timeout : 30,
  )
endif

# ------------------------------------------------------------------
# Performance benchmarks
# ------------------------------------------------------------------
bench_exe = executable('bench_render', 'bench_render.c',
  dependencies : [flux_dep])

benchmark('render', bench_exe,
  timeout : 120,
  env     : {
    'FLUX_BENCH_BACKEND' : 'software',
  },
)
