project('x11minesweeper', 'c',
  version : '0.1',
  default_options : [
    'buildtype=debugoptimized',
    'c_std=c99',
    'warning_level=3',
  ])

add_project_arguments('-D_XOPEN_SOURCE=500', language: 'c')

xcb = dependency('xcb')
png = dependency('libpng')

exe = executable(
  'x11minesweeper',
  'main.c',
  dependencies: [xcb, png],
  install : true
)

test('basic', exe)
