project('libmobile-bgb', 'c',
  version : '0.2.0',
  default_options : [
    'warning_level=2',
    'c_std=gnu11'])

cc = meson.get_compiler('c')
cc_args = []
cc_ld_args = []
deps = []

# Enable -ffunction-sections -fdata-sections and -Wl,--gc-sections by default
if cc.has_link_argument('-Wl,--gc-sections')
  cc_ld_args += '-Wl,--gc-sections'
  cc_args += ['-ffunction-sections', '-fdata-sections']
endif

add_global_arguments(cc_args, language : 'c')
add_global_link_arguments(cc_ld_args, language : 'c')

# Link relevant libraries
deps += dependency('libmobile', version : '>=0.2.0', static : true)
deps += dependency('threads')

if host_machine.system() == 'windows'
  deps += cc.find_library('ws2_32')
endif

executable('mobile',
  'source/bgblink.c',
  'source/main.c',
  'source/socket.c',
  install : true,
  dependencies : deps)
