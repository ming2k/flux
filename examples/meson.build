# Examples — offscreen demos that write PPM images.
# These do not require a display server or windowing toolkit.

executable('minimal',
  'minimal.c',
  dependencies : [flux_dep],
  install : false,
)

executable('hello_rect',
  'hello_rect.c',
  dependencies : [flux_dep],
  install : false,
)

# Windowed example — requires GLFW and a Vulkan-capable display.
glfw_dep = dependency('glfw3', required : false)
if glfw_dep.found()
  executable('windowed',
    'windowed.c',
    dependencies : [flux_dep, glfw_dep, m_dep],
    install : false,
  )
endif
