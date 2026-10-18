harness = static_library(
	'harness',
	sources: [ 'scene.c', 'engine.c', 'tracker.c', 'sim.c' ],
	dependencies: damage_dep,
)

harness_dep = declare_dependency(
	link_with: harness,
	include_directories: include_directories('.'),
	dependencies: damage_dep,
)

executable('damage-sim', 'damage-sim.c', dependencies: harness_dep)
