project(
  'diffusa',
  'cpp',
  default_options: {
    'cpp_std': 'c++20',
    'warning_level': '3',
    'buildtype': 'release',
    'b_ndebug': 'true',
  },
)

fmt_dep = dependency('fmt')
gtkmm_dep = dependency('gtkmm-4.0')
libadwaita_dep = dependency('libadwaita-1')
mupdf_dep = dependency('mupdf')

cpp = meson.get_compiler('cpp')
mupdfcpp_deps = cpp.find_library('mupdfcpp')

executable(
  'hirgon',
  'src/main.cpp',
  install: true,
  include_directories: ['include'],
  dependencies: [fmt_dep, gtkmm_dep, libadwaita_dep, mupdf_dep] + mupdfcpp_deps,
)
