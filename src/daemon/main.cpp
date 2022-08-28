// SPDX-License-Identifier: GPL-2.0-or-later

#include "context.hpp"
#include "devices.hpp"
#include "dft.hpp"
#include "stylus.hpp"
#include "touch.hpp"

#include <common/signal.hpp>
#include <common/types.hpp>
#include <ipts/device.hpp>
#include <ipts/parser.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

using namespace std::chrono;

namespace iptsd::daemon {

static int main(gsl::span<char *> args)
{
	if (args.size() < 2)
		throw std::runtime_error("You need to specify the hidraw device!");

	std::atomic_bool should_exit = false;

	auto const _sigterm = common::signal<SIGTERM>([&](int) { should_exit = true; });
	auto const _sigint = common::signal<SIGINT>([&](int) { should_exit = true; });

	Context ctx {0x045E,0x0C1A};

	ipts::Parser parser {};
	parser.on_stylus = [&](const auto &data) { iptsd_stylus_input(ctx, data); };
	parser.on_heatmap = [&](const auto &data) { iptsd_touch_input(ctx, data); };
	parser.on_dft = [&](const auto &dft, auto &stylus) { iptsd_dft_input(ctx, dft, stylus); };

	struct {
		uint8_t hdr_size;
		uint8_t reserved[3];
		uint32_t msg_num;
		uint32_t size;
	} hdr;
	struct {
		uint32_t type;
		uint32_t size;
		uint32_t unused[14];
		uint8_t buffer[0x10000];
	} data;

	int fd = open(args[1], O_RDONLY);
	if (fd < 0) throw common::cerror("Failed to open file");

	for (int i = 0; i < 100; i++) {
		lseek(fd, 0, 0);
		while (true) {
			ssize_t r = read(fd, &hdr, sizeof hdr);
			if (r != sizeof hdr) break;
			ssize_t size = read(fd, &data, hdr.size);
			if (size != hdr.size) throw common::cerror("Failed to read data");
			if (data.type != 3) continue;

			parser.parse(gsl::span<u8>(data.buffer, data.size));
		}
	}

	spdlog::info("Stopping");

	return EXIT_FAILURE;
}

} // namespace iptsd::daemon

int main(int argc, char **argv)
{
	spdlog::set_pattern("[%X.%e] [%^%l%$] %v");

	try {
		return iptsd::daemon::main(gsl::span<char *>(argv, argc));
	} catch (std::exception &e) {
		spdlog::error(e.what());
		return EXIT_FAILURE;
	}
}
