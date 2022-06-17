// SPDX-License-Identifier: GPL-2.0-or-later

#include "parser.hpp"

#include <bitset>

namespace iptsd::ipts {

// Block:

template <class T> inline const T& Block::read()
{
	if (sizeof(T) > remaining()) throw std::out_of_range(typeid(T).name());
	T* r = (T*)&data[index];
	index += sizeof(T);
	return *r;
}

void Block::skip(const size_t size)
{
	if (size > remaining()) throw std::out_of_range("skip");
	index += size;
}

size_t Block::remaining()
{
	return end - index;
}

Block Block::block(size_t size)
{
	if (size > remaining()) throw std::out_of_range("block");
	size_t start = index;
	index += size;
	return Block(data, start, index);
}

gsl::span<const u8> Block::span()
{
	return gsl::span<const u8>(&data[index], end - index);
}

// Parser:

Block Parser::block()
{
	return Block(data, 0, data.size());
}

void Parser::reset()
{
	heatmap.data = gsl::span<const u8>();
}

void Parser::parse(Block &b, bool ignore_truncated)
{
	const auto header = b.read<struct ipts_data>();
	if (ignore_truncated && header.size > b.remaining()) return;
	auto data = b.block(header.size);

	switch (header.type) {
	case IPTS_DATA_TYPE_PAYLOAD:
		parse_payload(data);
		break;
	case IPTS_DATA_TYPE_HID_REPORT:
		parse_hid(data);
		break;
	}
}

void Parser::parse()
{
	reset();
	auto b = block();
	parse(b, false);
}

void Parser::parse_loop()
{
	reset();
	auto b = block();
	while (b.remaining())
		parse(b, false);
}

void Parser::parse_ithc(size_t size)
{
	struct ithc_api_header {
		uint8_t hdr_size;
		uint8_t reserved[3];
		uint32_t msg_num;
		uint32_t size;
	};

	reset();
	auto b = block().block(size);
	while (b.remaining()) {
		const auto hdr = b.read<struct ithc_api_header>();
		b.skip(hdr.hdr_size - sizeof hdr);
		auto data = b.block(hdr.size);
		parse(data, true);
	}
}

void Parser::parse_payload(Block &b)
{
	const auto payload = b.read<struct ipts_payload>();

	for (u32 i = 0; i < payload.frames; i++) {
		const auto frame = b.read<struct ipts_payload_frame>();
		auto data = b.block(frame.size);

		switch (frame.type) {
		case IPTS_PAYLOAD_FRAME_TYPE_STYLUS:
			parse_stylus(data);
			break;
		case IPTS_PAYLOAD_FRAME_TYPE_HEATMAP:
			parse_container_reports(data);
			break;
		}
	}
}

void Parser::parse_hid(Block &b)
{
	const auto report_code = b.read<u8>();

	if (report_code == IPTS_HID_REPORT_SINGLETOUCH)
		parse_singletouch(b);
	else if (IPTS_HID_REPORT_IS_CONTAINER(report_code))
		parse_hid_container(b);
}

void Parser::parse_singletouch(Block &b)
{
	const auto singletouch = b.read<struct ipts_singletouch_data>();

	SingletouchData data;
	data.touch = singletouch.touch;
	data.x = singletouch.x;
	data.y = singletouch.y;

	if (on_singletouch)
		on_singletouch(data);
}

void Parser::parse_stylus(Block &b)
{
	// TODO could merge this into parse_container_reports?
	while (b.remaining()) {
		const auto report = b.read<struct ipts_report>();
		auto data = b.block(report.size);

		switch (report.type) {
		case IPTS_REPORT_TYPE_STYLUS_V1:
			parse_stylus_report_v1(data);
			break;
		case IPTS_REPORT_TYPE_STYLUS_V2:
			parse_stylus_report_v2(data);
			break;
		}
	}
}

void Parser::parse_stylus_report_v1(Block &b)
{
	StylusData stylus;

	const auto stylus_report = b.read<struct ipts_stylus_report>();
	stylus.serial = stylus_report.serial;

	for (u8 i = 0; i < stylus_report.elements; i++) {
		const auto data = b.read<struct ipts_stylus_data_v1>();

		const std::bitset<8> mode(data.mode);
		stylus.proximity = mode[IPTS_STYLUS_REPORT_MODE_BIT_PROXIMITY];
		stylus.contact = mode[IPTS_STYLUS_REPORT_MODE_BIT_CONTACT];
		stylus.button = mode[IPTS_STYLUS_REPORT_MODE_BIT_BUTTON];
		stylus.rubber = mode[IPTS_STYLUS_REPORT_MODE_BIT_RUBBER];

		stylus.x = data.x;
		stylus.y = data.y;
		stylus.pressure = data.pressure * 4;
		stylus.azimuth = 0;
		stylus.altitude = 0;
		stylus.timestamp = 0;

		if (on_stylus)
			on_stylus(stylus);
	}
}

void Parser::parse_stylus_report_v2(Block &b)
{
	StylusData stylus;

	const auto stylus_report = b.read<struct ipts_stylus_report>();
	stylus.serial = stylus_report.serial;

	for (u8 i = 0; i < stylus_report.elements; i++) {
		const auto data = b.read<struct ipts_stylus_data_v2>();

		const std::bitset<16> mode(data.mode);
		stylus.proximity = mode[IPTS_STYLUS_REPORT_MODE_BIT_PROXIMITY];
		stylus.contact = mode[IPTS_STYLUS_REPORT_MODE_BIT_CONTACT];
		stylus.button = mode[IPTS_STYLUS_REPORT_MODE_BIT_BUTTON];
		stylus.rubber = mode[IPTS_STYLUS_REPORT_MODE_BIT_RUBBER];

		stylus.x = data.x;
		stylus.y = data.y;
		stylus.pressure = data.pressure;
		stylus.azimuth = data.azimuth;
		stylus.altitude = data.altitude;
		stylus.timestamp = data.timestamp;

		if (on_stylus)
			on_stylus(stylus);
	}
}

void Parser::parse_hid_container(Block &b)
{
	const auto timestamp = b.read<uint16_t>();
	(void)timestamp;
	const auto root = b.read<struct ipts_hid_container>();
	auto root_data = b.block(root.size - sizeof root);

	while (root_data.remaining()) {
		const auto c = root_data.read<struct ipts_hid_container>();
		// XXX On SP7 we receive 0x74 packets with 4 nul bytes of data, inside a container with an incorrect size. Let's just ignore these.
		if (root.size == 22 && c.type == 0xff && c.size == 11) return;
		auto data = root_data.block(c.size - sizeof c);
		switch (c.type) {
			case 1:
			{
				const auto hm = data.read<struct ipts_hid_heatmap>();
				heatmap.data = data.block(hm.size).span();
				break;
			}
			case 0xff:
			{
				parse_container_reports(data);
				break;
			}
		}
	}
}

void Parser::parse_dft(Block &data)
{
	const auto dft = data.read<struct ipts_pen_dft_window>();
	const struct ipts_pen_dft_window_row *dft_x[IPTS_DFT_MAX_ROWS], *dft_y[IPTS_DFT_MAX_ROWS];
	if (dft.num_rows > IPTS_DFT_MAX_ROWS) return;
	for (size_t i = 0; i < dft.num_rows; i++) dft_x[i] = &data.read<struct ipts_pen_dft_window_row>();
	for (size_t i = 0; i < dft.num_rows; i++) dft_y[i] = &data.read<struct ipts_pen_dft_window_row>();
	process_dft(dft, dft_x, dft_y);
}

void Parser::parse_container_reports(Block &b)
{
	const struct ipts_report_start *start = nullptr;
	const struct ipts_heatmap_dim *dim = nullptr;

	while (b.remaining()) {
		const auto report = b.read<struct ipts_report>();
		auto data = b.block(report.size);

		switch (report.type) {
		case IPTS_REPORT_TYPE_START:
			start = &data.read<struct ipts_report_start>();
			break;
		case IPTS_REPORT_TYPE_HEATMAP_DIM:
			dim = &data.read<struct ipts_heatmap_dim>();
			num_cols = dim->width;
			num_rows = dim->height;
			break;
		case IPTS_REPORT_TYPE_HEATMAP:
			if (dim) heatmap.data = data.block(dim->width * dim->height).span();
			break;
		case IPTS_REPORT_TYPE_PEN_DFT_WINDOW:
			parse_dft(data);
			break;
		}
	}

	if (on_heatmap && heatmap.data.size() && start && dim)
	{
		heatmap.timestamp = start->timestamp;
		heatmap.height = dim->height;
		heatmap.width = dim->width;
		heatmap.y_min = dim->y_min;
		heatmap.y_max = dim->y_max;
		heatmap.x_min = dim->x_min;
		heatmap.x_max = dim->x_max;
		heatmap.z_min = dim->z_min;
		// z_min/z_max are both 0 in the HID data, which
		// doesnt make sense. Lets use sane values instead.
		heatmap.z_max = dim->z_max ? dim->z_max : 255;
		on_heatmap(heatmap);
	}
}

} // namespace iptsd::ipts
