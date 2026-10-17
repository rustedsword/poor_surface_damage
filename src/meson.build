damage = library(
	'poor_surface_damage',
	sources: [ 'damage.c' ],
	version: meson.project_version(),
	soversion: '0',
	dependencies: poor_base_dep,
	install: true
)

damage_dep = declare_dependency(
	link_with: damage,
	include_directories: include_directories('.'),
	dependencies: poor_base_dep,
)

pkgconfig.generate(
	damage,
	name: 'poor_surface_damage',
	description: 'Damage tracking for Wayland surfaces',
	version: meson.project_version(),
	filebase: 'poor_surface_damage',
	requires: [ 'poor_base' ]
)

meson.override_dependency('poor_surface_damage', damage_dep)

install_headers('poor_surface_damage.h')
