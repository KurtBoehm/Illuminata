project(
  'hirgon',
  'cpp',
  version: '1.0.0',
  license: 'GPL-3.0-or-later',
  default_options: {
    'cpp_std': 'c++20',
    'warning_level': '3',
    'buildtype': 'release',
    'b_ndebug': 'true',
  },
)

i18n = import('i18n')
gnome = import('gnome')
desktop_file_validate = find_program('desktop-file-validate', required: false)

application_id = 'org.kurbo96.Hirgon'
prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
iconsdir = datadir / 'icons'
podir = meson.project_source_root() / 'po'

subdir('data')

fmt_dep = dependency('fmt')
epoxy_dep = dependency('epoxy')
gtkmm_dep = dependency('gtkmm-4.0')
libadwaita_dep = dependency('libadwaita-1')
libadwaitamm_dep = dependency('libadwaitamm-1')
mupdf_dep = dependency('mupdf')

cpp = meson.get_compiler('cpp')
mupdfcpp_deps = cpp.find_library('mupdfcpp')

opengl = get_option('opengl')
print = get_option('print')
version = meson.project_version()
args = [
  f'-DHIRGON_OPENGL=@opengl@',
  f'-DHIRGON_PRINT=@print@',
  f'-DHIRGON_VERSION="@version@"',
]

executable(
  'hirgon',
  'src/main.cpp',
  install: true,
  include_directories: ['include'],
  cpp_args: args,
  dependencies: [
    epoxy_dep,
    fmt_dep,
    gtkmm_dep,
    libadwaita_dep,
    libadwaitamm_dep,
    mupdf_dep,
  ]
  + mupdfcpp_deps,
)

gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)
