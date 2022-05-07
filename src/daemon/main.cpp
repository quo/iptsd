// SPDX-License-Identifier: GPL-2.0-or-later

#include "config.hpp"
#include "context.hpp"
#include "devices.hpp"
#include "singletouch.hpp"
#include "stylus.hpp"
#include "touch.hpp"

#include <common/signal.hpp>
#include <common/types.hpp>
#include <ipts/control.hpp>
#include <ipts/ipts.h>
#include <ipts/parser.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fmt/format.h>
#include <functional>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <thread>

using namespace std::chrono;

namespace iptsd::daemon {

static bool iptsd_loop(Context &ctx, ipts::Control &control)
{
	u32 doorbell = control.doorbell();
	u32 diff = doorbell - control.current_doorbell;

	while (doorbell > control.current_doorbell) {
		control.read(ctx.parser.buffer());

		try {
			ctx.parser.parse();
		} catch (std::out_of_range &e) {
			spdlog::error(e.what());
		}

		control.send_feedback();
	}

	return diff > 0;
}

static int main_ipts()
{

	std::atomic_bool should_exit {false};
	std::atomic_bool should_reset {false};

	auto const _sigusr1 = common::signal<SIGUSR1>([&](int) { should_reset = true; });
	auto const _sigterm = common::signal<SIGTERM>([&](int) { should_exit = true; });
	auto const _sigint = common::signal<SIGINT>([&](int) { should_exit = true; });

	ipts::Control control;
	struct ipts_device_info info = control.info;
	Context ctx(info);
	system_clock::time_point timeout = system_clock::now() + 5s;

	spdlog::info("Connected to device {:04X}:{:04X}", info.vendor, info.product);

	ctx.parser.on_singletouch = [&](const auto &data) { iptsd_singletouch_input(ctx, data); };
	ctx.parser.on_stylus = [&](const auto &data) { iptsd_stylus_input(ctx, data); };
	ctx.parser.on_heatmap = [&](const auto &data) { iptsd_touch_input(ctx, data); };

	while (true) {
		if (iptsd_loop(ctx, control))
			timeout = system_clock::now() + 5s;

		std::this_thread::sleep_for(timeout > system_clock::now() ? 10ms : 200ms);

		if (should_reset) {
			spdlog::info("Resetting touch sensor");

			control.reset();
			should_reset = false;
		}

		if (should_exit) {
			spdlog::info("Stopping");

			return EXIT_FAILURE;
		}
	}

	return 0;
}

#define ITHC_DEV "/dev/ithc"
#define ITHC_SYSFS "/sys/class/misc/ithc/device/ithc/"

static int main_ithc()
{
	struct ipts_device_info info = { 0 };
	std::ifstream(ITHC_SYSFS "vendor") >> std::setbase(0) >> info.vendor;
	std::ifstream(ITHC_SYSFS "product") >> std::setbase(0) >> info.product;
	spdlog::info("Vendor/product: {:04X}:{:04X}", info.vendor, info.product);
	info.buffer_size = 0x10000;
	info.max_contacts = 10;
	Context ctx(info);
	ctx.parser.on_singletouch = [&](const auto &data) { iptsd_singletouch_input(ctx, data); };
	ctx.parser.on_stylus = [&](const auto &data) { iptsd_stylus_input(ctx, data); };
	ctx.parser.on_heatmap = [&](const auto &data) { iptsd_touch_input(ctx, data); };

	int fd = common::open(ITHC_DEV, O_RDONLY);
	if (fd < 0) throw common::cerror("Failed to open " ITHC_DEV);
	lseek(fd, 0, SEEK_END);
	spdlog::info("Opened " ITHC_DEV);

	while (true) {
		ssize_t r = common::read(fd, ctx.parser.buffer());
		if (r < 0 && errno == EINTR) break;
		if (r < 0) throw common::cerror("Failed to read from buffer");

		try {
			ctx.parser.parse_ithc(r);
		} catch (std::out_of_range &e) {
			spdlog::error(e.what());
		}
	}

	return 0;
}

} // namespace iptsd::daemon

int main()
{
	spdlog::set_pattern("[%X.%e] [%^%l%$] %v");

	try {
		if (!access(ITHC_DEV, F_OK)) return iptsd::daemon::main_ithc();
		return iptsd::daemon::main_ipts();
	} catch (std::exception &e) {
		spdlog::error(e.what());
		return EXIT_FAILURE;
	}
}
