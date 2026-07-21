project(
  'linux-capture-studio',
  'c',
  version: '0.6.30',
  default_options: ['warning_level=2', 'c_std=c17']
)

gtk4_dep = dependency('gtk4', version: '>=4.14')
gstreamer_dep = dependency('gstreamer-1.0', version: '>=1.20')
cc = meson.get_compiler('c')
math_dep = cc.find_library('m', required: true)

subdir('src')


install_data(
  'packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.desktop',
  install_dir: get_option('datadir') / 'applications'
)
install_data(
  'packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo'
)
install_data(
  'packaging/io.github.linuxcapturestudio.LinuxCaptureStudio.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps'
)
install_data(
  'scripts/run-flatpak.sh',
  rename: 'linux-capture-studio-flatpak',
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x'
)
