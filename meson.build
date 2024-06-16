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
epoxy_dep = dependency('epoxy')
gtkmm_dep = dependency('gtkmm-4.0')
libadwaita_dep = dependency('libadwaita-1')
mupdf_dep = dependency('mupdf')

cpp = meson.get_compiler('cpp')
mupdfcpp_deps = cpp.find_library('mupdfcpp')

opengl = get_option('opengl')
print = get_option('print')
args = [
  f'-DHIRGON_OPENGL=@opengl@',
  f'-DHIRGON_PRINT=@print@',
]

executable(
  'hirgon',
  'src/main.cpp',
  install: true,
  include_directories: ['include'],
  cpp_args: args,
  dependencies: [epoxy_dep, fmt_dep, gtkmm_dep, libadwaita_dep, mupdf_dep] + mupdfcpp_deps,
)
