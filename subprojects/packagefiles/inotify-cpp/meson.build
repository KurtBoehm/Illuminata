project('inotify-cpp', 'cpp', default_options: ['cpp_std=c++23'])

inc = include_directories('src/include')
inotify_cpp_lib = static_library(
  'inotify-cpp',
  [
    'src/Event.cpp',
    'src/FileSystemEvent.cpp',
    'src/Inotify.cpp',
    'src/Notification.cpp',
    'src/NotifierBuilder.cpp',
  ],
  include_directories: inc,
)
inotify_cpp_dep = declare_dependency(
  include_directories: inc,
  link_with: inotify_cpp_lib,
)
