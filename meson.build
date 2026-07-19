project('byte', 'c',
  version: '0.1.0',
  meson_version: '>=1.0.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ]
)

add_project_arguments('-fPIC', language: 'c')

if get_option('debug')
  add_project_arguments('-DDEBUG', language: 'c')
endif

sources = files(
  'src/main.c',
  'src/compiler/compiler.c',
  'src/compiler/scanner.c',
  'src/compiler/vm.c',
  'src/core/chunk.c',
  'src/core/value.c',
  'src/debug/debug.c',
  'src/utils/memory.c',
)

includes = include_directories(
  'src',
  'src/compiler',
  'src/core',
  'src/debug',
  'src/utils',
)

m_dep = meson.get_compiler('c').find_library('m')

byte_exe = executable('byte',
  sources,
  include_directories: includes,
  dependencies: [m_dep],
)
