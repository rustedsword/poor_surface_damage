project(
	'poor_surface_damage', 'c',
	version: '0.1.0',
	license: 'MIT',
	license_files: 'LICENSE',
	meson_version: '>= 1.4.0',
	default_options: [
		'warning_level=2',
		'buildtype=debugoptimized',
		'c_std=c23',
	]
)

pkgconfig = import('pkgconfig')

config_h = configuration_data()
config_h.set_quoted('PACKAGE', meson.project_name())
config_h.set_quoted('PACKAGE_VERSION', meson.project_version())

cc = meson.get_compiler('c')
add_project_arguments(
	cc.get_supported_arguments([
		'-Wno-unused-parameter',
		'-Wstrict-prototypes',
		'-Wmissing-prototypes',
		'-fvisibility=hidden',
		# Array views of no elements are variable length arrays of zero length
		'-fno-sanitize=vla-bound',
	]),
	language: 'c'
)

foreach h: [ 'sys/prctl.h' ]
	config_h.set('HAVE_' + h.underscorify().to_upper(), cc.has_header(h))
endforeach

foreach f: [ 'prctl' ]
	config_h.set('HAVE_' + f.underscorify().to_upper(), cc.has_function(f))
endforeach

configure_file(
	output: 'config.h',
	configuration: config_h,
)

# An installed poor_base is used if there is one, the submodule otherwise
poor_base_dep = dependency('poor_base')

root_inc = include_directories('.')

subdir('src')

if get_option('tests')
	subdir('tests')
endif
