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

if get_option('tools')
	executable('damage-sim', 'damage-sim.c', dependencies: harness_dep)

	if cc.has_header('linux/perf_event.h')
		executable('damage-bench', 'damage-bench.c', dependencies: harness_dep)
	endif
endif
