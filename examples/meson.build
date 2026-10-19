vulkan_dep = dependency('vulkan')
wayland_client_dep = dependency('wayland-client')
wayland_protocols_dep = dependency('wayland-protocols')
wayland_scanner = find_program('wayland-scanner')
glslc = find_program('glslc')

xdg_shell_xml = wayland_protocols_dep.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
xdg_shell = [
	custom_target(
		'xdg-shell-client-protocol.h',
		input: xdg_shell_xml,
		output: 'xdg-shell-client-protocol.h',
		command: [ wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@' ],
	),
	custom_target(
		'xdg-shell-protocol.c',
		input: xdg_shell_xml,
		output: 'xdg-shell-protocol.c',
		command: [ wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@' ],
	),
]

shaders = []
foreach shader : [ 'box.vert', 'box.frag' ]
	shaders += custom_target(
		shader + '.spv.h',
		input: shader,
		output: shader + '.spv.h',
		command: [ glslc, '--target-env=vulkan1.3', '-mfmt=c', '-o', '@OUTPUT@', '@INPUT@' ],
	)
endforeach

vk_damage = executable(
	'vk-damage',
	[ 'vk-damage.c' ] + xdg_shell + shaders,
	dependencies: [ harness_dep, vulkan_dep, wayland_client_dep ],
)

executable('shm-damage', [ 'shm-damage.c' ] + xdg_shell, dependencies: [ harness_dep, wayland_client_dep ])

if get_option('tests')
	foreach target : [ 'offscreen', 'headless' ]
		foreach tracker : [ 'buffers', 'history', 'map' ]
			test(
				'vk-damage-@0@-@1@'.format(target, tracker),
				vk_damage,
				args: [ '--target', target, '--tracker', tracker, '--images', '4', '--frames', '300',
					'--resize', '97', '--verify' ],
				suite: 'vulkan',
				timeout: 120,
				# Vulkan drivers leak in libraries they unload, which LeakSanitizer can't tell apart from real leaks
				env: [ 'ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1' ],
			)
		endforeach
	endforeach
endif
