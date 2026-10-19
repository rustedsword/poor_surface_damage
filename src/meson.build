damage = library(
	'poor_surface_damage',
	sources: [ 'region.c', 'buffers.c', 'history.c', 'map.c' ],
	include_directories: inc,
	version: meson.project_version(),
	soversion: '0',
	dependencies: poor_base_dep,
	install: true
)

damage_dep = declare_dependency(
	link_with: damage,
	include_directories: inc,
	dependencies: poor_base_dep,
)

pkgconfig.generate(
	damage,
	name: 'poor_surface_damage',
	description: 'Damage tracking without allocations',
	version: meson.project_version(),
	filebase: 'poor_surface_damage',
	requires: [ 'poor_base' ]
)

meson.override_dependency('poor_surface_damage', damage_dep)

install_headers(
	'../include/poor_surface_damage.h',
	'../include/poor_damage_region.h',
	'../include/poor_damage_surface.h',
	'../include/poor_damage_buffers.h',
	'../include/poor_damage_history.h',
	'../include/poor_damage_map.h',
)
