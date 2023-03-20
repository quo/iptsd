// SPDX-License-Identifier: GPL-2.0-or-later

#include "perf.hpp"

#include <common/signal.hpp>
#include <common/types.hpp>
#include <core/linux/file-runner.hpp>

#include <CLI/CLI.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <gsl/gsl>
#include <spdlog/spdlog.h>

namespace iptsd::apps::perf {

static int main(const gsl::span<char *> args)
{
	using std::chrono::duration_cast;
	using usecs = std::chrono::duration<f64, std::micro>;

	CLI::App app {};

	usize runs = 10;
	std::filesystem::path path {};

	app.add_option("DATA", path, "The binary data file containing the data to test.")
		->type_name("FILE")
		->required();

	app.add_option("RUNS", runs, "Repeat this number of runs through the data.")
		->check(CLI::Range(1, 1000));

	CLI11_PARSE(app, args.size(), args.data());

	// Create a performance testing application that reads from a file.
	core::linux::FileRunner<Perf> perf {path};

	const auto _sigterm = common::signal<SIGTERM>([&](int) { perf.stop(); });
	const auto _sigint = common::signal<SIGINT>([&](int) { perf.stop(); });

	using clock = std::chrono::high_resolution_clock;

	usize total = 0;
	usize total_of_squares = 0;
	usize count = 0;

	clock::duration min = clock::duration::max();
	clock::duration max = clock::duration::min();

	bool should_stop = false;

	for (usize i = 0; i < runs; i++) {
		should_stop = perf.run();

		Perf &app = perf.application();

		total += app.total;
		total_of_squares += app.total_of_squares;
		count += app.count;

		min = std::min(min, app.min);
		max = std::max(max, app.max);

		if (should_stop)
			break;

		app.reset();
	}

	const f64 n = gsl::narrow<f64>(count);
	const f64 mean = gsl::narrow<f64>(total) / n;
	const f64 stddev = std::sqrt(gsl::narrow<f64>(total_of_squares) / n - mean * mean);

	spdlog::info("Ran {} times", count);
	spdlog::info("Total: {}μs", total);
	spdlog::info("Mean: {:.2f}μs", mean);
	spdlog::info("Standard Deviation: {:.2f}μs", stddev);
	spdlog::info("Minimum: {:.3f}μs", duration_cast<usecs>(min).count());
	spdlog::info("Maximum: {:.3f}μs", duration_cast<usecs>(max).count());

	if (!should_stop)
		return EXIT_FAILURE;

	return 0;
}

} // namespace iptsd::apps::perf

int main(int argc, char **argv)
{
	spdlog::set_pattern("[%X.%e] [%^%l%$] %v");
	const gsl::span<char *> args {argv, gsl::narrow<usize>(argc)};

	try {
		return iptsd::apps::perf::main(args);
	} catch (std::exception &e) {
		spdlog::error(e.what());
		return EXIT_FAILURE;
	}
}
