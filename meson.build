project(
  'illuminata',
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

opengl = get_option('opengl')
print = get_option('print')
version = meson.project_version()

i18n = import('i18n')
gnome = import('gnome')
desktop_file_validate = find_program('desktop-file-validate', required: false)

application_id = 'org.kurbo96.Illuminata'
prefix = get_option('prefix')
datadir = prefix / get_option('datadir')
iconsdir = datadir / 'icons'
podir = meson.project_source_root() / 'po'

subdir('data')

deps = [
  dependency('fmt'),
  dependency('gtkmm-4.0'),
  dependency('libadwaita-1'),
  dependency('libadwaitamm-1'),
  dependency('mupdf'),
]
if opengl
  deps += dependency('epoxy')
endif

cpp = meson.get_compiler('cpp')
mupdfcpp_deps = cpp.find_library('mupdfcpp')

args = [
  f'-DILLUMINATA_OPENGL=@opengl@',
  f'-DILLUMINATA_PRINT=@print@',
  f'-DILLUMINATA_VERSION="@version@"',
]

executable(
  'illuminata',
  'src/main.cpp',
  install: true,
  include_directories: ['include'],
  cpp_args: args,
  dependencies: deps + mupdfcpp_deps,
)

gnome.post_install(
  gtk_update_icon_cache: true,
  update_desktop_database: true,
)
