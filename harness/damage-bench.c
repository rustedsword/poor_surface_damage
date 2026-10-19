/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <errno.h>
#include <getopt.h>
#include <linux/perf_event.h>
#include <poor_stdio.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "engine.h"
#include "scene.h"
#include "tracker.h"

constexpr uint32_t BENCH_MAX_FRAMES = 10000;
constexpr size_t BENCH_MAX_PIXELS = 3840 * 2160;
constexpr uint32_t BENCH_SEED = 12345;

enum workload { WORK_SCENE, WORK_SCENE_HD, WORK_WIDGETS, WORK_SCATTER, WORK_TYPING };

static const struct {
	const char *name;
	int32_t width, height;
} workloads[] = {
	[WORK_SCENE] = { "scene", 640, 480 },	    [WORK_SCENE_HD] = { "scene-hd", 1920, 1080 },
	[WORK_WIDGETS] = { "widgets", 1920, 1080 }, [WORK_SCATTER] = { "scatter", 3840, 2160 },
	[WORK_TYPING] = { "typing", 1920, 1080 },
};

enum { MARK_COVERED = 1, MARK_LACKED = 2 };

struct bench {
	struct tracker_options tracker;
	enum workload work;
	enum engine_order order;
	size_t buffers;
	int32_t width, height;
	uint32_t frames, reps;
};

struct outcome {
	/* Pixels redrawn, frames whose repair was wrong, and pixels in the exact union of the damage buffers missed */
	uint64_t redrawn, wrong, exact;
};

static region damages[BENCH_MAX_FRAMES];
static size_t indices[BENCH_MAX_FRAMES];
static uint8_t marks[BENCH_MAX_PIXELS];
static struct tracker tracker;
static int counter;

#define make_marks(name, bench)										\
	make_arrview_first(name##_all, (size_t)(bench)->width * (size_t)(bench)->height, marks);		\
	make_arrview_dim(name, (size_t)(bench)->width, name##_all)

[[noreturn]] static void usage(const char *name)
{
	printerrln("Usage: ", name, " [options]\n\n"
		   "Replays the damage of a few workloads through the trackers, with every buffer order of the engine, 1 to ",
		   ENGINE_BUFFERS, " buffers\nand 4 to 64 rects, without drawing. For each run it prints:\n"
		   "  cycles   CPU cycles spent in the tracker per frame, the least of all repetitions\n"
		   "  drawn    share of them spent in drawn()\n"
		   "  redrawn  share of the pixels that were redrawn\n"
		   "With --verify it checks every repair pixel by pixel instead, and prints:\n"
		   "  wrong    frames whose repair missed damage, or had rects that touch\n"
		   "  /exact   redrawn pixels compared to the exact union of the damage that buffers missed\n\n"
		   "Workloads:\n"
		   "  scene     the scene of damage-sim at 640x480\n"
		   "  scene-hd  the same at 1920x1080\n"
		   "  widgets   16 widgets on 1920x1080 that change at random, and everything every 600 frames\n"
		   "  scatter   12 small rects at random places on 3840x2160 every frame\n"
		   "  typing    a character typed per frame on 1920x1080, and a clock that ticks every 30 frames\n\n"
		   "Cycles are counted with perf, which needs kernel.perf_event_paranoid = -1.\n\n"
		   "  --tracker T   run only this tracker: whole, naive, buffers, history or map\n"
		   "                (default: buffers, history and map)\n"
		   "  --work W      run only this workload\n"
		   "  --order O     run only this buffer order: fifo, newest or random\n"
		   "  --buffers N   run only this number of buffers\n"
		   "  --rects N     run only this number of rects, up to 64\n"
		   "  --frames N    number of frames, up to ", BENCH_MAX_FRAMES, " (default: 3000, or 400 with --verify)\n"
		   "  --reps N      repetitions of each run (default: 9)\n"
		   "  --verify      check the repairs instead of timing them\n"
		   TRACKER_STORAGE_USAGE);
	exit(EXIT_FAILURE);
}

static void open_counter(void)
{
	struct perf_event_attr attr = { .type = PERF_TYPE_HARDWARE, .size = sizeof(attr),
					.config = PERF_COUNT_HW_CPU_CYCLES, .pinned = 1, .exclude_kernel = 1,
					.exclude_hv = 1 };
	FILE *paranoid = fopen("/proc/sys/kernel/perf_event_paranoid", "r");
	int level;

	if (!paranoid || fscanf(paranoid, "%d", &level) != 1 || level != -1) {
		printerrln("kernel.perf_event_paranoid must be -1");
		exit(EXIT_FAILURE);
	}
	fclose(paranoid);
	counter = (int)syscall(SYS_perf_event_open, &attr, 0, -1, -1, 0);
	if (counter < 0) {
		printerrln("perf_event_open: ", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/* Stay on the current CPU, as CPUs of different kinds take different cycles */
static void pin(void)
{
	cpu_set_t cpus;

	CPU_ZERO(&cpus);
	CPU_SET(sched_getcpu(), &cpus);
	sched_setaffinity(0, sizeof(cpus), &cpus);
}

static uint64_t cycles(void)
{
	uint64_t value;

	if (read(counter, &value, sizeof(value)) != (ssize_t)sizeof(value)) {
		printerrln("another perf session took the cycle counter");
		exit(EXIT_FAILURE);
	}
	return value;
}

static int32_t lesser(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static int32_t greater(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static uint32_t next(uint32_t *random)
{
	*random ^= *random << 13;
	*random ^= *random >> 17;
	*random ^= *random << 5;
	return *random;
}

static int32_t between(uint32_t *random, int32_t low, int32_t high)
{
	return low + (int32_t)(next(random) % (uint32_t)(high - low + 1));
}

/* Record the damage of every frame, limited to the rects of the tracker as an application with that capacity would */
static void record(const struct bench *bench)
{
	struct poor_damage_rect widgets[16];
	uint32_t random = BENCH_SEED;
	int32_t column = 0, line = 0;
	struct scene scene;

	scene_init(&scene, bench->width, bench->height, BENCH_SEED);
	foreach_array_ref(widgets, widget) {
		widget->width = between(&random, 24, 320);
		widget->height = between(&random, 16, 120);
		widget->x = between(&random, 0, bench->width - widget->width);
		widget->y = between(&random, 0, bench->height - widget->height);
	}
	foreach_array_index(arrview_first(bench->frames, damages), frame) {
		region damage = POOR_DAMAGE_REGION_INIT;

		if (!frame)
			poor_damage_region_whole(&damage, bench->width, bench->height);
		switch (bench->work) {
		case WORK_SCENE:
		case WORK_SCENE_HD:
			scene_step(&scene, &damage);
			break;
		case WORK_WIDGETS:
			foreach_array_const_ref(widgets, widget)
				if (next(&random) % 8 == 0)
					poor_damage_region_add(&damage, *widget);
			if (frame % 600 == 599)
				poor_damage_region_whole(&damage, bench->width, bench->height);
			break;
		case WORK_SCATTER:
			for (int i = 0; i < 12; i++) {
				int32_t width = between(&random, 8, 128), height = between(&random, 8, 128);
				int32_t x = between(&random, 0, bench->width - width);
				int32_t y = between(&random, 0, bench->height - height);

				poor_damage_region_add(&damage, (struct poor_damage_rect){ x, y, width, height });
			}
			break;
		case WORK_TYPING:
			poor_damage_region_add(&damage, (struct poor_damage_rect){ 100 + 9 * column, 100 + 18 * line, 11, 18 });
			column = (column + 1) % 80;
			line = (line + !column) % 50;
			poor_damage_region_add(&damage, (struct poor_damage_rect){ 100 + 9 * column, 100 + 18 * line, 2, 18 });
			if (frame % 30 == 0)
				poor_damage_region_add(&damage, (struct poor_damage_rect){ 1700, 20, 60, 18 });
			break;
		}
		poor_damage_region_clip(&damage, bench->width, bench->height);
		damages[frame].count = 0;
		poor_damage_rects_add_rects(arrview_first(bench->tracker.rects, damages[frame].rects), &damages[frame].count,
					    arrview_first(damage.count, damage.rects));
	}
}

static void schedule(const struct bench *bench)
{
	struct engine engine;

	engine_init(&engine, bench->order, bench->buffers, BENCH_SEED);
	foreach_array_ref(arrview_first(bench->frames, indices), index) {
		*index = engine_acquire(&engine);
		engine_present(&engine, *index);
	}
}

/* The least cycles that the tracker spends on all frames, repairing them only if 'repair' */
static uint64_t least(const struct bench *bench, bool repair)
{
	uint64_t best = UINT64_MAX;

	for (uint32_t i = 0; i < bench->reps; i++) {
		uint64_t spent;
		region out;

		tracker_init(&tracker, &bench->tracker, bench->buffers, bench->width, bench->height);
		spent = cycles();
		foreach_array_index(arrview_first(bench->frames, indices), frame) {
			if (repair)
				tracker_repair(&tracker, indices[frame], &damages[frame], &out);
			tracker_drawn(&tracker, indices[frame], &damages[frame]);
			tracker_presented(&tracker, indices[frame]);
		}
		spent = cycles() - spent;
		best = spent < best ? spent : best;
	}
	return best;
}

static bool apart(const struct poor_damage_rect *a, const struct poor_damage_rect *b)
{
	int64_t a_right = (int64_t)a->x + a->width, a_bottom = (int64_t)a->y + a->height;
	int64_t b_right = (int64_t)b->x + b->width, b_bottom = (int64_t)b->y + b->height;
	bool overlap_x = a->x < b_right && b->x < a_right, touch_x = a->x <= b_right && b->x <= a_right;
	bool overlap_y = a->y < b_bottom && b->y < a_bottom, touch_y = a->y <= b_bottom && b->y <= a_bottom;

	return !(overlap_x && touch_y) && !(touch_x && overlap_y);
}

/* Whether a repair fits the rects of the tracker and its rects never touch, as in every region */
static bool sound(const struct bench *bench, const region *repair)
{
	if (repair->count > bench->tracker.rects)
		return false;
	foreach_array_index(arrview_first(repair->count, repair->rects), i)
		foreach_array_const_ref(arrview(i + 1, repair->count - i - 1, repair->rects), other)
			if (!apart(&repair->rects[i], other))
				return false;
	return true;
}

/* Mark the part of 'rect' that 'repair' covers */
static void cover(const struct bench *bench, const struct poor_damage_rect *rect, const region *repair)
{
	make_marks(view, bench);

	foreach_array_const_ref(arrview_first(repair->count, repair->rects), part) {
		int32_t x = greater(rect->x, part->x), y = greater(rect->y, part->y);
		int32_t right = lesser(rect->x + rect->width, part->x + part->width);
		int32_t bottom = lesser(rect->y + rect->height, part->y + part->height);

		if (right <= x || bottom <= y)
			continue;
		foreach_array_ref(arrview((size_t)y, (size_t)(bottom - y), *view), row)
			foreach_array_ref(arrview((size_t)x, (size_t)(right - x), *row), mark)
				*mark |= MARK_COVERED;
	}
}

/* Mark 'rect' as lacked, count its pixels lacked for the first time, and return how many of those weren't covered */
static uint64_t lack(const struct bench *bench, const struct poor_damage_rect *rect, uint64_t *exact)
{
	uint64_t missed = 0;
	make_marks(view, bench);

	foreach_array_ref(arrview((size_t)rect->y, (size_t)rect->height, *view), row) {
		foreach_array_ref(arrview((size_t)rect->x, (size_t)rect->width, *row), mark) {
			if (*mark & MARK_LACKED)
				continue;
			*mark |= MARK_LACKED;
			*exact += 1;
			missed += !(*mark & MARK_COVERED);
		}
	}
	return missed;
}

static void unmark(const struct bench *bench, const struct poor_damage_rect *rect)
{
	make_marks(view, bench);

	foreach_array_ref(arrview((size_t)rect->y, (size_t)rect->height, *view), row)
		fill_array(arrview((size_t)rect->x, (size_t)rect->width, *row), 0);
}

/*
 * Replay the frames once more to count the redrawn pixels, and if 'verify', check every repair against the damage of
 * the frames the buffer missed. A buffer that was never drawn into missed the first frame, which damages everything.
 */
static void inspect(const struct bench *bench, bool verify, struct outcome *outcome)
{
	size_t seen[TRACKER_MAX_BUFFERS];
	region repair;

	outcome->redrawn = outcome->wrong = outcome->exact = 0;
	fill_array(arrview_first(bench->buffers, seen), 0);
	tracker_init(&tracker, &bench->tracker, bench->buffers, bench->width, bench->height);
	foreach_array_index(arrview_first(bench->frames, indices), frame) {
		size_t index = indices[frame];

		tracker_repair(&tracker, index, &damages[frame], &repair);
		foreach_array_const_ref(arrview_first(repair.count, repair.rects), rect)
			outcome->redrawn += (uint64_t)rect->width * (uint64_t)rect->height;
		if (verify) {
			make_arrview(missed, seen[index], frame + 1 - seen[index], damages);
			uint64_t failed = !sound(bench, &repair);

			foreach_array_const_ref(missed, damage) {
				foreach_array_const_ref(arrview_first(damage->count, damage->rects), rect) {
					cover(bench, rect, &repair);
					failed += lack(bench, rect, &outcome->exact);
				}
			}
			foreach_array_const_ref(missed, damage)
				foreach_array_const_ref(arrview_first(damage->count, damage->rects), rect)
					unmark(bench, rect);
			outcome->wrong += failed > 0;
			seen[index] = frame + 1;
		}
		tracker_drawn(&tracker, index, &damages[frame]);
		tracker_presented(&tracker, index);
	}
}

/* Time or check one run and print its row. Returns false if a frame came out wrong. */
static bool run(const struct bench *bench, bool verify)
{
	double pixels = (double)bench->width * bench->height * bench->frames;
	struct outcome outcome;
	uint64_t all, drawn;

	schedule(bench);
	inspect(bench, verify, &outcome);
	print(fmt_w(tracker_name(bench->tracker.kind), -8), fmt_w(workloads[bench->work].name, -9),
	      fmt_w(engine_order_name(bench->order), -7), fmt_w(bench->buffers, 8), fmt_w(bench->tracker.rects, 6));
	if (verify) {
		println(fmt_w(outcome.wrong, 9), fmt_wp(100 * (double)outcome.redrawn / pixels, 8, 2), "%",
			fmt_wp((double)outcome.redrawn / (double)outcome.exact, 8, 2));
		return !outcome.wrong;
	}
	all = least(bench, true);
	drawn = least(bench, false);
	println(fmt_w(all / bench->frames, 9), fmt_wp(100 * (double)drawn / (double)all, 6, 1), "%",
		fmt_wp(100 * (double)outcome.redrawn / pixels, 8, 2), "%");
	return true;
}

static bool parse_work(const char *name, enum workload *work)
{
	foreach_array_index(workloads, i) {
		if (!strcmp(workloads[i].name, name)) {
			*work = (enum workload)i;
			return true;
		}
	}
	return false;
}

static bool parse_order(const char *name, enum engine_order *order)
{
	for (enum engine_order each = ENGINE_FIFO; each <= ENGINE_RANDOM; each++) {
		if (!strcmp(engine_order_name(each), name)) {
			*order = each;
			return true;
		}
	}
	return false;
}

int main(int argc, char *argv[])
{
	static const struct option long_options[] = {
		{ "work", required_argument, nullptr, 'w' }, { "order", required_argument, nullptr, 'o' },
		{ "buffers", required_argument, nullptr, 'b' }, { "frames", required_argument, nullptr, 'f' },
		{ "reps", required_argument, nullptr, 'r' }, { "verify", no_argument, nullptr, 'v' },
		TRACKER_LONG_OPTIONS, { "help", no_argument, nullptr, '?' }, {},
	};
	struct bench bench = { .tracker = TRACKER_OPTIONS_INIT, .frames = 0, .reps = 9 };
	enum tracker_kind first = TRACKER_BUFFERS, last = TRACKER_MAP;
	enum workload first_work = WORK_SCENE, last_work = WORK_TYPING;
	enum engine_order first_order = ENGINE_FIFO, last_order = ENGINE_RANDOM;
	size_t rects[] = { 4, 8, 16, 64 }, capacities = ARRAY_SIZE(rects);
	size_t fewest = 1, most = ENGINE_BUFFERS;
	bool known = true, verify = false, right = true;
	int option;

	while (known && (option = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
		switch (option) {
		case 'w':
			known = parse_work(optarg, &first_work);
			last_work = first_work;
			break;
		case 'o':
			known = parse_order(optarg, &first_order);
			last_order = first_order;
			break;
		case 'b':
			fewest = most = strtoul(optarg, nullptr, 10);
			known = fewest && fewest <= ENGINE_BUFFERS;
			break;
		case 'f':
			bench.frames = (uint32_t)strtoul(optarg, nullptr, 10);
			known = bench.frames && bench.frames <= BENCH_MAX_FRAMES;
			break;
		case 'r':
			bench.reps = (uint32_t)strtoul(optarg, nullptr, 10);
			known = bench.reps;
			break;
		case 'v':
			verify = true;
			break;
		case TRACKER_OPTION_KIND:
			known = tracker_option(&bench.tracker, option, optarg);
			first = last = bench.tracker.kind;
			break;
		case TRACKER_OPTION_RECTS:
			known = tracker_option(&bench.tracker, option, optarg);
			rects[0] = bench.tracker.rects;
			capacities = 1;
			break;
		default:
			known = tracker_option(&bench.tracker, option, optarg);
		}
	}
	if (!known || optind != argc)
		usage(argv[0]);
	open_counter();
	pin();
	if (!bench.frames)
		bench.frames = verify ? 400 : 3000;

	print(fmt_w("tracker", -8), fmt_w("work", -9), fmt_w("order", -7), fmt_w("buffers", 8), fmt_w("rects", 6));
	if (verify)
		println(fmt_w("wrong", 9), fmt_w("redrawn", 9), fmt_w("/exact", 8));
	else
		println(fmt_w("cycles", 9), fmt_w("drawn", 7), fmt_w("redrawn", 9));
	for (bench.tracker.kind = first; bench.tracker.kind <= last; bench.tracker.kind++) {
		for (bench.work = first_work; bench.work <= last_work; bench.work++) {
			bench.width = workloads[bench.work].width;
			bench.height = workloads[bench.work].height;
			foreach_array_const_ref(arrview_first(capacities, rects), capacity) {
				bench.tracker.rects = *capacity;
				record(&bench);
				for (bench.order = first_order; bench.order <= last_order; bench.order++)
					for (bench.buffers = fewest; bench.buffers <= most; bench.buffers++)
						right &= run(&bench, verify);
			}
		}
	}
	return right ? EXIT_SUCCESS : EXIT_FAILURE;
}
