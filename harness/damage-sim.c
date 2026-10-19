/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <getopt.h>
#include <poor_stdio.h>
#include <stdlib.h>

#include "sim.h"

[[noreturn]] static void usage(const char *name)
{
	printerrln("Usage: ", name, " [options]\n\n"
		   "Simulates every tracker, or just one, with every buffer order of the engine and 1 to ", ENGINE_BUFFERS,
		   " buffers.\nFor each run it prints:\n"
		   "  wrong    frames that came out wrong\n"
		   "  whole    frames that redrew everything\n"
		   "  redrawn  share of the pixels that were redrawn\n"
		   "  /exact   redrawn pixels compared to the exact union of the damage that buffers missed\n"
		   "  /needed  redrawn pixels compared to the pixels that actually changed\n"
		   "  rects    rects per frame\n"
		   "  ns       time spent in the tracker per frame\n\n"
		   "  --width W, --height H  surface size, up to ", SIM_WIDTH, "x", SIM_HEIGHT, " (default: 640x480)\n"
		   "  --frames N    number of frames (default: 2000)\n"
		   "  --resize N    change the surface size every N frames\n"
		   "  --seed N      random seed of the scene\n"
		   "  --tracker T   run only this tracker: whole, naive, buffers, history or map\n"
		   TRACKER_CAPACITY_USAGE);
	exit(EXIT_FAILURE);
}

static double ratio(uint64_t a, uint64_t b)
{
	return b ? (double)a / (double)b : 0;
}

int main(int argc, char *argv[])
{
	static const struct option long_options[] = {
		{ "width", required_argument, nullptr, 'w' }, { "height", required_argument, nullptr, 'h' },
		{ "frames", required_argument, nullptr, 'f' }, { "resize", required_argument, nullptr, 'r' },
		{ "seed", required_argument, nullptr, 's' }, TRACKER_LONG_OPTIONS, { "help", no_argument, nullptr, '?' }, {},
	};
	struct sim_options options = {
		.tracker = TRACKER_OPTIONS_INIT, .width = 640, .height = 480, .frames = 2000, .resize = 0, .seed = 1,
	};
	enum tracker_kind first = TRACKER_WHOLE, last = TRACKER_MAP;
	struct sim_result result;
	bool known = true;
	int option;

	while (known && (option = getopt_long(argc, argv, "", long_options, nullptr)) != -1) {
		switch (option) {
		case 'w': options.width = atoi(optarg); break;
		case 'h': options.height = atoi(optarg); break;
		case 'f': options.frames = (uint32_t)atoi(optarg); break;
		case 'r': options.resize = (uint32_t)atoi(optarg); break;
		case 's': options.seed = (uint32_t)atoi(optarg); break;
		case TRACKER_OPTION_KIND:
			known = tracker_option(&options.tracker, option, optarg);
			first = last = options.tracker.kind;
			break;
		default:
			known = tracker_option(&options.tracker, option, optarg);
		}
	}
	if (!known || optind != argc || options.width <= 0 || options.width > SIM_WIDTH || options.height <= 0 ||
	    options.height > SIM_HEIGHT || !options.seed)
		usage(argv[0]);

	println(fmt_w("tracker", -8), fmt_w("order", -8), fmt_w("buffers", 8), fmt_w("wrong", 7), fmt_w("whole", 7),
		fmt_w("redrawn", 9), fmt_w("/exact", 8), fmt_w("/needed", 8), fmt_w("rects", 6), fmt_w("ns", 7));
	for (enum tracker_kind tracker = first; tracker <= last; tracker++) {
		for (enum engine_order order = ENGINE_FIFO; order <= ENGINE_RANDOM; order++) {
			for (size_t buffers = 1; buffers <= ENGINE_BUFFERS; buffers++) {
				options.tracker.kind = tracker;
				options.order = order;
				options.buffers = buffers;
				sim_run(&options, &result);
				println(fmt_w(tracker_name(tracker), -8), fmt_w(engine_order_name(order), -8),
					fmt_w(buffers, 8), fmt_w(result.wrong, 7),
					fmt_wp(100 * ratio(result.wholes, result.frames), 6, 1), "%",
					fmt_wp(100 * ratio(result.drawn, result.pixels), 8, 2), "%",
					fmt_wp(ratio(result.drawn, result.exact), 8, 2),
					fmt_wp(ratio(result.drawn, result.needed), 8, 2),
					fmt_wp(ratio(result.rects, result.frames), 6, 2),
					fmt_w(result.nanoseconds / result.frames, 7));
			}
		}
	}
	return EXIT_SUCCESS;
}
