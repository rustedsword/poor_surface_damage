test_runner = static_library(
	'test-runner',
	sources: [
		'test-runner.c',
		'test-helpers.c',
	],
	include_directories: root_inc,
	dependencies: [
		cc.find_library('dl', required: false),
		dependency('threads'),
		poor_base_dep,
	]
)

test_runner_dep = declare_dependency(
	link_with: test_runner,
	dependencies: [
		dependency('threads'),
		cc.find_library('dl', required: false)
	]
)

foreach test_name : [ 'region-test', 'buffers-test', 'history-test', 'map-test' ]
	test(test_name, executable(test_name, test_name + '.c', dependencies: [ test_runner_dep, damage_dep ]))
endforeach

test('sim-test', executable('sim-test', 'sim-test.c', dependencies: [ test_runner_dep, harness_dep ]))
