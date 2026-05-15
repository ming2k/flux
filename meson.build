project('flux', ['c', 'cpp'],
  version : '0.2.1',
  license : 'GPL-3.0',
  default_options : [
    'c_std=c23',
    'warning_level=2',
    'default_library=shared',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  '-D_GNU_SOURCE',
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
  language : 'c',
)

vulkan_dep   = dependency('vulkan',        version : '>=1.2', required : false)
threads_dep  = dependency('threads')
m_dep        = cc.find_library('m', required : false)

flux_inc = include_directories('include', 'src')

subdir('src')

if get_option('examples')
  subdir('examples')
endif

if get_option('tests')
  subdir('tests')
endif

pkg = import('pkgconfig')
pkg.generate(
  libraries   : flux_lib,
  name        : 'flux',
  filebase    : 'flux',
  description : 'Low-level 2D graphics foundation with software and Vulkan backends',
  version     : meson.project_version(),
)
