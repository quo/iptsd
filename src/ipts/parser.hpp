/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IPTSD_IPTS_PARSER_HPP
#define IPTSD_IPTS_PARSER_HPP

#include "protocol.h"

#include <common/types.hpp>

#include <cstddef>
#include <functional>
#include <gsl/gsl>
#include <memory>
#include <vector>

namespace iptsd::ipts {

class SingletouchData {
public:
	bool touch = false;
	u16 x = 0;
	u16 y = 0;
};

class StylusData {
public:
	bool proximity = false;
	bool contact = false;
	bool button = false;
	bool rubber = false;

	u16 timestamp = 0;
	u16 x = 0;
	u16 y = 0;
	u16 pressure = 0;
	u16 altitude = 0;
	u16 azimuth = 0;
	u32 serial = 0;
};

class Heatmap {
public:
	u8 width = 0;
	u8 height = 0;

	u8 y_min = 0;
	u8 y_max = 0;
	u8 x_min = 0;
	u8 x_max = 0;
	u8 z_min = 0;
	u8 z_max = 0;
	u32 timestamp = 0;

	gsl::span<const u8> data;
};

class Block {
private:
	std::vector<u8> &data;
	size_t index, end;

public:
	Block(std::vector<u8> &data, size_t index, size_t end) : data(data), index(index), end(end) {};
	template <class T> const T& read();
	void skip(const size_t size);
	size_t remaining();
	Block block(size_t size);
	gsl::span<const u8> span();
};

class Parser {
private:
	std::vector<u8> data;
	bool invert_x, invert_y;

	Heatmap heatmap;
	StylusData stylus;
	int stylus_real = 0, stylus_imag = 0;
	int num_cols = 0, num_rows = 0;

	Block block();

	void reset();
	void parse(Block&, bool);
	void parse_payload(Block&);
	void parse_hid(Block&);

	void parse_singletouch(Block&);
	void parse_hid_container(Block&);
	void parse_container_reports(Block&);
	void parse_heatmap_data(Block&);

	void parse_stylus(Block&);
	void parse_stylus_report_v1(Block&);
	void parse_stylus_report_v2(Block&);

	void parse_dft(Block&);
	void process_dft(const struct ipts_pen_dft_window &dft,
		const struct ipts_pen_dft_window_row **dft_x,
		const struct ipts_pen_dft_window_row **dft_y);
	void stop_stylus();

public:
	std::function<void(const SingletouchData &)> on_singletouch;
	std::function<void(const StylusData &)> on_stylus;
	std::function<void(const Heatmap &)> on_heatmap;

	Parser(size_t size, bool invert_x = false, bool invert_y = false) : data(size), invert_x(invert_x), invert_y(invert_y) {};

	const gsl::span<u8> buffer();
	void parse();
	void parse_loop();
	void parse_ithc(size_t len);
};

inline const gsl::span<u8> Parser::buffer()
{
	return gsl::span(this->data);
}

} /* namespace iptsd::ipts */

#endif /* IPTSD_IPTS_PARSER_HPP */
