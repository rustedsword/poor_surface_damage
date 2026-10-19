/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <getopt.h>
#include <poor_mman.h>
#include <poor_poll.h>
#include <poor_stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#include "scene.h"
#include "tracker.h"
#include "xdg-shell-client-protocol.h"

/*
 * A wl_shm client that draws the test scene. It picks a buffer among the ones that the compositor released, and either
 * redraws what that buffer is missing, or copies it from the last presented buffer and redraws only the damage of the
 * new frame. A new frame is drawn when a frame callback arrives, or after 100 ms without one, since hidden windows get
 * no frame callbacks.
 */

constexpr size_t BUFFERS = 4;

struct buffer {
	struct wl_buffer *buffer;
	uint32_t *pixels;
	uint64_t presented;
	bool busy;
};

struct options {
	struct tracker_options tracker;
	size_t buffers;
	uint32_t frames, seed;
	int32_t width, height;
	bool copy, oldest, verify;
};

struct app {
	struct options options;
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	bool configured, closed, ready;
	struct buffer buffers[BUFFERS];
	uint32_t *reference;
	size_t front;
	struct scene scene;
	struct tracker tracker;
	uint64_t frame;
	struct {
		uint64_t wrong, drawn, copied;
	} stats;
};

[[noreturn]] static void fail(const char *message)
{
	printerrln("shm-damage: ", message);
	exit(EXIT_FAILURE);
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
			    uint32_t version)
{
	struct app *app = data;

	if (!strcmp(interface, wl_compositor_interface.name) && version >= 4)
		app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
	if (!strcmp(interface, wl_shm_interface.name))
		app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	if (!strcmp(interface, xdg_wm_base_interface.name))
		app->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
	xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
	struct app *app = data;

	xdg_surface_ack_configure(xdg_surface, serial);
	app->configured = true;
	app->ready = true;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure,
};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height,
			       struct wl_array *states)
{
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
	struct app *app = data;

	app->closed = true;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_configure,
	.close = toplevel_close,
};

static void buffer_release(void *data, struct wl_buffer *wl_buffer)
{
	struct buffer *buffer = data;

	buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_release,
};

static void frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	struct app *app = data;

	wl_callback_destroy(callback);
	app->ready = true;
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_done,
};

static uint32_t *map(size_t size, int fd)
{
	uint32_t (*pixels)[size] = mmap_array(pixels, nullptr, PROT_READ | PROT_WRITE,
					      fd < 0 ? MAP_PRIVATE | MAP_ANONYMOUS : MAP_SHARED, fd, 0);

	if (pixels == MAP_FAILED)
		fail("can't map pixels");
	return *pixels;
}

static void create_buffer(struct app *app, struct buffer *buffer)
{
	size_t size = (size_t)app->options.width * (size_t)app->options.height;
	int fd = memfd_create("shm-damage", MFD_CLOEXEC);
	struct wl_shm_pool *pool;

	if (fd < 0 || ftruncate(fd, (off_t)(size * sizeof(uint32_t))) < 0)
		fail("can't create a buffer");
	buffer->pixels = map(size, fd);
	pool = wl_shm_create_pool(app->shm, fd, (int32_t)(size * sizeof(uint32_t)));
	buffer->buffer = wl_shm_pool_create_buffer(pool, 0, app->options.width, app->options.height,
						   app->options.width * (int32_t)sizeof(uint32_t), WL_SHM_FORMAT_XRGB8888);
	wl_buffer_add_listener(buffer->buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);
	buffer->presented = 0;
	buffer->busy = false;
}

static void init(struct app *app)
{
	app->display = wl_display_connect(nullptr);
	if (!app->display)
		fail("no Wayland display");
	app->compositor = nullptr;
	app->shm = nullptr;
	app->wm_base = nullptr;
	app->registry = wl_display_get_registry(app->display);
	wl_registry_add_listener(app->registry, &registry_listener, app);
	wl_display_roundtrip(app->display);
	if (!app->compositor || !app->shm || !app->wm_base)
		fail("the compositor lacks wl_compositor 4, wl_shm or xdg_wm_base");
	xdg_wm_base_add_listener(app->wm_base, &wm_base_listener, app);
	app->surface = wl_compositor_create_surface(app->compositor);
	app->xdg_surface = xdg_wm_base_get_xdg_surface(app->wm_base, app->surface);
	xdg_surface_add_listener(app->xdg_surface, &xdg_surface_listener, app);
	app->toplevel = xdg_surface_get_toplevel(app->xdg_surface);
	xdg_toplevel_add_listener(app->toplevel, &toplevel_listener, app);
	xdg_toplevel_set_title(app->toplevel, "shm-damage");
	xdg_toplevel_set_app_id(app->toplevel, "shm-damage");
	app->configured = app->closed = false;
	app->ready = true;
	wl_surface_commit(app->surface);
	while (!app->configured && wl_display_dispatch(app->display) != -1)
		;
	foreach_array_ref(arrview_first(app->options.buffers, app->buffers), buffer)
		create_buffer(app, buffer);
	if (app->options.verify)
		app->reference = map((size_t)app->options.width * (size_t)app->options.height, -1);
	scene_init(&app->scene, app->options.width, app->options.height, app->options.seed);
	tracker_init(&app->tracker, &app->options.tracker, app->options.buffers, app->options.width, app->options.height);
	app->frame = 0;
	app->stats = (typeof(app->stats)){};
}

/* Pick a released buffer: the most recently presented one, or the oldest with --oldest. BUFFERS means none is free. */
static size_t pick(struct app *app)
{
	size_t best = BUFFERS;

	foreach_array_index(arrview_first(app->options.buffers, app->buffers), i) {
		const struct buffer *buffer = &app->buffers[i];

		if (buffer->busy)
			continue;
		if (best == BUFFERS || (app->options.oldest ? buffer->presented < app->buffers[best].presented :
							      buffer->presented > app->buffers[best].presented))
			best = i;
	}
	return best;
}

#define make_pixels(name, pixels, app)								\
	make_array_ptr(name##_all, pixels, (size_t)(app)->options.width * (size_t)(app)->options.height);	\
	make_arrview_dim(name, (size_t)(app)->options.width, name##_all)

static void copy_rect(struct app *app, const uint32_t *from, uint32_t *to, struct poor_damage_rect rect)
{
	make_pixels(source, from, app);
	make_pixels(target, to, app);

	foreach_array_index(arrview((size_t)rect.y, (size_t)rect.height, *target), i)
		copy_array(arrview((size_t)rect.x, (size_t)rect.width, (*target)[(size_t)rect.y + i]),
			   arrview((size_t)rect.x, (size_t)rect.width, (*source)[(size_t)rect.y + i]));
}

static uint64_t area(const region *region)
{
	uint64_t pixels = 0;

	foreach_array_const_ref(arrview_first(region->count, region->rects), rect)
		pixels += (uint64_t)rect->width * (uint64_t)rect->height;
	return pixels;
}

static void draw_frame(struct app *app, size_t index)
{
	struct buffer *buffer = &app->buffers[index];
	region damage = POOR_DAMAGE_REGION_INIT, nothing = POOR_DAMAGE_REGION_INIT, lacks, repair;
	make_pixels(pixels, buffer->pixels, app);

	if (!app->frame)
		poor_damage_region_whole(&damage, app->options.width, app->options.height);
	scene_step(&app->scene, &damage);
	poor_damage_region_clip(&damage, app->options.width, app->options.height);
	tracker_limit(&app->tracker, &damage);
	if (app->options.copy && app->frame) {
		tracker_repair(&app->tracker, index, &nothing, &lacks);
		foreach_array_const_ref(arrview_first(lacks.count, lacks.rects), rect)
			copy_rect(app, app->buffers[app->front].pixels, buffer->pixels, *rect);
		app->stats.copied += area(&lacks);
		repair = damage;
	} else {
		tracker_repair(&app->tracker, index, &damage, &repair);
	}
	foreach_array_const_ref(arrview_first(repair.count, repair.rects), rect)
		scene_draw(&app->scene, pixels, *rect);
	app->stats.drawn += area(&repair);
	tracker_drawn(&app->tracker, index, &damage);
	if (app->options.verify) {
		make_pixels(reference, app->reference, app);

		scene_draw(&app->scene, reference, ((struct poor_damage_rect){ 0, 0, app->options.width, app->options.height }));
		app->stats.wrong += !!memcmp(pixels_all, reference_all, ARRAY_SIZE_BYTES(pixels_all));
	}

	wl_surface_attach(app->surface, buffer->buffer, 0, 0);
	foreach_array_const_ref(arrview_first(damage.count, damage.rects), rect)
		wl_surface_damage_buffer(app->surface, rect->x, rect->y, rect->width, rect->height);
	wl_callback_add_listener(wl_surface_frame(app->surface), &frame_listener, app);
	wl_surface_commit(app->surface);
	tracker_presented(&app->tracker, index);
	buffer->busy = true;
	buffer->presented = ++app->frame;
	app->front = index;
	app->ready = false;
}

static void finish(struct app *app)
{
	size_t size = (size_t)app->options.width * (size_t)app->options.height;

	foreach_array_ref(arrview_first(app->options.buffers, app->buffers), buffer) {
		make_array_ptr(pixels, buffer->pixels, size);

		wl_buffer_destroy(buffer->buffer);
		munmap_array(pixels);
	}
	if (app->options.verify) {
		make_array_ptr(pixels, app->reference, size);

		munmap_array(pixels);
	}
	xdg_toplevel_destroy(app->toplevel);
	xdg_surface_destroy(app->xdg_surface);
	wl_surface_destroy(app->surface);
	xdg_wm_base_destroy(app->wm_base);
	wl_shm_destroy(app->shm);
	wl_compositor_destroy(app->compositor);
	wl_registry_destroy(app->registry);
	wl_display_disconnect(app->display);
}

static void wait(struct app *app)
{
	struct pollfd fds[] = { { .fd = wl_display_get_fd(app->display), .events = POLLIN } };

	while (wl_display_prepare_read(app->display))
		wl_display_dispatch_pending(app->display);
	if (wl_display_flush(app->display) < 0)
		fail("lost the Wayland display");
	if (poll_array(fds, 100) > 0) {
		if (wl_display_read_events(app->display) < 0)
			fail("lost the Wayland display");
	} else {
		wl_display_cancel_read(app->display);
		app->ready = true;
	}
	wl_display_dispatch_pending(app->display);
}

[[noreturn]] static void usage(const char *name)
{
	printerrln("Usage: ", name, " [options]\n\n"
		   "  --buffers N   number of buffers, up to ", BUFFERS, " (default: 3)\n"
		   "  --copy        copy what a buffer missed from the last presented one, and redraw only new damage\n"
		   "  --oldest      reuse the oldest released buffer instead of the most recent one\n"
		   "  --frames N    quit after N frames, 0 to run until the window is closed\n"
		   "  --size WxH    window size (default: 640x480)\n"
		   "  --seed N      random seed of the scene\n"
		   "  --verify      compare every frame with one drawn from scratch\n"
		   TRACKER_USAGE);
	exit(EXIT_FAILURE);
}

static void parse(int argc, char *argv[], struct options *options)
{
	static const struct option long_options[] = {
		TRACKER_LONG_OPTIONS, { "buffers", required_argument, nullptr, 'b' },
		{ "copy", no_argument, nullptr, 'c' }, { "oldest", no_argument, nullptr, 'o' },
		{ "frames", required_argument, nullptr, 'f' }, { "size", required_argument, nullptr, 's' },
		{ "seed", required_argument, nullptr, 'r' }, { "verify", no_argument, nullptr, 'v' },
		{ "help", no_argument, nullptr, 'h' }, {},
	};
	bool known;
	int option;

	*options = (struct options){
		.tracker = TRACKER_OPTIONS_INIT, .buffers = 3, .frames = 0, .seed = 1, .width = 640, .height = 480,
	};
	while ((option = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
		known = true;
		switch (option) {
		case 'b':
			options->buffers = (size_t)atoi(optarg);
			break;
		case 'c':
			options->copy = true;
			break;
		case 'o':
			options->oldest = true;
			break;
		case 'f':
			options->frames = (uint32_t)atoi(optarg);
			break;
		case 's':
			known = sscanf(optarg, "%dx%d", &options->width, &options->height) == 2 && options->width > 0 &&
				options->height > 0;
			break;
		case 'r':
			options->seed = (uint32_t)atoi(optarg);
			break;
		case 'v':
			options->verify = true;
			break;
		default:
			known = tracker_option(&options->tracker, option, optarg);
		}
		if (!known)
			usage(argv[0]);
	}
	if (optind != argc || !options->seed || !options->buffers || options->buffers > BUFFERS)
		usage(argv[0]);
}

int main(int argc, char *argv[])
{
	struct app app;
	size_t index;

	parse(argc, argv, &app.options);
	init(&app);
	while (!app.closed && (!app.options.frames || app.frame < app.options.frames)) {
		if (app.ready && (index = pick(&app)) != BUFFERS)
			draw_frame(&app, index);
		wait(&app);
	}
	if (app.frame)
		println("shm-damage: ", tracker_name(app.options.tracker.kind), ", ", app.options.buffers, " buffers: ",
			app.frame, " frames, ", app.options.verify ? "" : "unverified, ", app.stats.wrong, " wrong, ",
			fmt_p(100.0 * (double)app.stats.drawn / (double)(app.frame * (uint64_t)app.options.width *
									  (uint64_t)app.options.height), 2),
			"% of pixels drawn, ",
			fmt_p(100.0 * (double)app.stats.copied / (double)(app.frame * (uint64_t)app.options.width *
									   (uint64_t)app.options.height), 2),
			"% copied");
	finish(&app);
	return app.stats.wrong ? EXIT_FAILURE : EXIT_SUCCESS;
}
