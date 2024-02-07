// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef IPTSD_IPTS_PARSER_HPP
#define IPTSD_IPTS_PARSER_HPP

#include "data.hpp"
#include "protocol.hpp"
#include "protocol/hid.hpp"
#include "protocol/legacy.hpp"
#include "protocol/metadata.hpp"
#include "protocol/report.hpp"
#include "protocol/stylus.hpp"

#include <common/casts.hpp>
#include <common/reader.hpp>
#include <common/types.hpp>

#include <gsl/gsl>

#include <array>
#include <functional>
#include <optional>

namespace iptsd::ipts {

class Parser {
public:
	// The callback that is invoked when stylus data was parsed.
	std::function<void(const StylusData &)> on_stylus;

	// The callback that is invoked when a capacitive heatmap was parsed.
	std::function<void(const Heatmap &)> on_heatmap;

	// The callback that is invoked when a DFT window was parsed.
	std::function<void(const DftWindow &)> on_dft;

	// The callback that is invoked when a metadata report was parsed.
	std::function<void(const Metadata &)> on_metadata;

private:
	struct ipts_dimensions m_dim {};
	struct ipts_pen_metadata m_pen_meta {};

public:
	/*!
	 * Parses IPTS touch data from a HID report buffer.
	 *
	 * The data must have a three byte header, consisting of the report ID and a timestamp.
	 *
	 * @param[in] data The data to parse.
	 */
	void parse(const gsl::span<u8> data)
	{
		this->parse<protocol::hid::ReportHeader>(data);
	}

	/*!
	 * Parses IPTS touch data with an arbitrary header.
	 *
	 * @tparam T The type (and size) of the header.
	 * @param[in] data The data to parse.
	 */
	template <class T>
	void parse(const gsl::span<u8> data)
	{
		this->parse_with_header(data, sizeof(T));
	}

private:
	void parse_with_header(const gsl::span<u8> data, const usize header)
	{
		Reader reader(data);
		reader.skip(header);

		this->parse_hid_frame(reader);
	}

	/*!
	 * Parses an IPTS HID frame.
	 *
	 * These are the top-level data structure in the data received from the device.
	 * For more information, see @ref protocol::hid::Frame
	 *
	 * @param[in] reader The chunk of data allocated to the HID frame.
	 */
	void parse_hid_frame(Reader &reader)
	{
		const auto frame = reader.read<protocol::hid::Frame>();
		Reader sub = reader.sub(frame.size - sizeof(frame));

		switch (frame.type) {
		case protocol::hid::FrameType::Hid:
			this->parse_hid_frames(sub);
			break;
		case protocol::hid::FrameType::Heatmap:
			this->parse_heatmap_frame(sub);
			break;
		case protocol::hid::FrameType::Metadata:
			this->parse_metadata_frame(sub);
			break;
		case protocol::hid::FrameType::Legacy:
			this->parse_legacy_frame(sub);
			break;
		case protocol::hid::FrameType::Reports:
			/*
			 * On SP7 we receive the following data about once per second:
			 * 16 00 00 00 00 00 00
			 *   0b 00 00 00 00 ff 00
			 *     74 00 04 00 00 00 00 00
			 * This causes a parse error, because the "0b" should be "0f".
			 * So let's just ignore these packets.
			 */
			if (reader.size() == 4)
				return;

			this->parse_report_frames(sub);
			break;
		default:
			// TODO: Add handler for unknow data and wire up debug tools
			break;
		}
	}

	/*!
	 * Parses a list of IPTS HID frames.
	 *
	 * @param[in] reader The chunk of data allocated to the HID frames.
	 */
	void parse_hid_frames(Reader &reader)
	{
		while (reader.size() > 0)
			this->parse_hid_frame(reader);
	}

	/*!
	 * Parses legacy frames.
	 *
	 * Legacy frames are used by older devices, that don't natively support HID.
	 * For more information, see @ref protocol::legacy::Header
	 *
	 * @param[in] reader The chunk of data allocated to the legacy frame.
	 */
	void parse_legacy_frame(Reader &reader)
	{
		const auto header = reader.read<protocol::legacy::Header>();

		for (u32 i = 0; i < header.elements; i++) {
			const auto group = reader.read<protocol::legacy::ReportGroup>();
			Reader sub = reader.sub(group.size);

			switch (group.type) {
			case protocol::legacy::GroupType::Stylus:
			case protocol::legacy::GroupType::Touch:
				this->parse_report_frames(sub);
				break;
			default:
				// TODO: Add handler for unknow data and wire up debug tools
				break;
			}
		}
	}

	/*!
	 * Parses an IPTS metadata frame.
	 *
	 * Metadata frames are returned by a HID feature report on devices that natively support
	 * HID. Once the data is parsed, the @ref on_metadata callback will be invoked.
	 *
	 * @param[in] reader The chunk of data allocated to the metadata frame.
	 */
	void parse_metadata_frame(Reader &reader) const
	{
		Metadata m {};

		m.dimensions = reader.read<protocol::metadata::Dimensions>();
		m.unknown_byte = reader.read<u8>();
		m.transform = reader.read<protocol::metadata::Transform>();
		m.unknown = reader.read<protocol::metadata::Unknown>();

		if (this->on_metadata)
			this->on_metadata(m);
	}

	/*!
	 * Parses an IPTS report frame.
	 *
	 * Report frames can be found inside of HID and legacy frames. They contain very specific
	 * data from the touchscreen, such as stylus coordintes or capacitive heatmaps.
	 *
	 * @param[in] reader The chunk of data allocated to the report frame.
	 */
	void parse_report_frame(Reader &reader)
	{
		const auto frame = reader.read<protocol::report::Frame>();
		Reader sub = reader.sub(frame.size);

		switch (frame.type) {
		case protocol::report::Type::StylusMPP_1_0:
			this->parse_stylus_mpp_1_0(sub);
			break;
		case protocol::report::Type::StylusMPP_1_51:
			this->parse_stylus_mpp_1_51(sub);
			break;
		case protocol::report::Type::HeatmapDimensions:
			this->parse_dimensions(sub);
			break;
		case protocol::report::Type::HeatmapData:
			this->parse_heatmap_data(sub);
			break;
		case protocol::report::Type::DftMetadata:
			this->parse_pen_metadata(sub);
			break;
		case protocol::report::Type::DftWindow:
			this->parse_dft_window(sub);
			break;
		default:
			// TODO: Add handler for unknow data and wire up debug tools
			break;
		}
	}

	/*!
	 * Parses a list of IPTS report frames.
	 *
	 * @param[in] reader The chunk of data allocated to the list of report frames.
	 */
	void parse_report_frames(Reader &reader)
	{
		while (reader.size() > 0)
			this->parse_report_frame(reader);
	}

	/*!
	 * Parses an MPP (Microsoft Pen Protocol) 1.0 stylus report.
	 *
	 * These support 1024 levels of pressure, and have no tilt information.
	 *
	 * Stylus reports can contains multiple samples of the stylus state from a 5
	 * millisecond window. Only the last sample is processed, the others are dropped to
	 * prevent a jittering output.
	 *
	 * @param[in] reader The chunk of data allocated to the report frame.
	 */
	void parse_stylus_mpp_1_0(Reader &reader) const
	{
		const auto report = reader.read<protocol::stylus::Report>();

		for (u8 i = 0; i < report.samples - 1; i++)
			reader.skip(sizeof(protocol::stylus::SampleMPP_1_0));

		const auto sample = reader.read<protocol::stylus::SampleMPP_1_0>();

		if (!this->on_stylus)
			return;

		StylusData data {};
		data.serial = report.serial;

		data.proximity = sample.state.proximity;
		data.button = sample.state.button;
		data.rubber = sample.state.rubber;

		// sample.state.contact is always false when the stylus is in eraser mode
		data.contact = sample.pressure > 0;

		data.x = casts::to<f64>(sample.x);
		data.y = casts::to<f64>(sample.y);
		data.pressure = casts::to<f64>(sample.pressure);

		data.x /= protocol::stylus::MAX_X;
		data.y /= protocol::stylus::MAX_Y;
		data.pressure /= protocol::stylus::MAX_PRESSURE_MPP_1_0;

		data.altitude = 0;
		data.azimuth = 0;
		data.timestamp = 0;

		this->on_stylus(data);
	}

	/*!
	 * Parses an MPP (Microsoft Pen Protocol) 1.51 stylus report.
	 *
	 * These support 4096 levels of pressure, and have tilt information.
	 *
	 * Stylus reports can contains multiple samples of the stylus state from a 5
	 * millisecond window. Only the last sample is processed, the others are dropped to
	 * prevent a jittering output.
	 *
	 * @param[in] reader The chunk of data allocated to the report frame.
	 */
	void parse_stylus_mpp_1_51(Reader &reader) const
	{
		const auto report = reader.read<protocol::stylus::Report>();

		for (u8 i = 0; i < report.samples - 1; i++)
			reader.skip(sizeof(protocol::stylus::SampleMPP_1_51));

		const auto sample = reader.read<protocol::stylus::SampleMPP_1_51>();

		if (!this->on_stylus)
			return;

		StylusData data {};
		data.serial = report.serial;
		data.timestamp = sample.timestamp;

		data.proximity = sample.state.proximity;
		data.button = sample.state.button;
		data.rubber = sample.state.rubber;

		// sample.state.contact is always false when the stylus is in eraser mode
		data.contact = sample.pressure > 0;

		data.x = casts::to<f64>(sample.x);
		data.y = casts::to<f64>(sample.y);
		data.pressure = casts::to<f64>(sample.pressure);

		data.x /= protocol::stylus::MAX_X;
		data.y /= protocol::stylus::MAX_Y;
		data.pressure /= protocol::stylus::MAX_PRESSURE_MPP_1_51;

		data.altitude = casts::to<f64>(sample.altitude);
		data.azimuth = casts::to<f64>(sample.azimuth);

		data.altitude /= 18000.0 / M_PI;
		data.azimuth /= 18000.0 / M_PI;

		this->on_stylus(data);
	}

	/*!
	 * Parses a heatmap dimensions report.
	 *
	 * This report describes the scale and size of a heatmap sent by IPTS.
	 * While IPTS will send a heatmap report and a dimensions report in the same
	 * frame, the size of the heatmap is defined by the *previous* dimensions report.
	 *
	 * This means that the dimensions have to be cached until the full heatmap has been parsed.
	 *
	 * @param[in] reader The chunk of data allocated to the report.
	 */
	void parse_dimensions(Reader &reader)
	{
		m_dim = reader.read<struct ipts_dimensions>();

		// On newer devices, z_max may be 0, lets use a sane value instead.
		if (m_dim.z_max == 0)
			m_dim.z_max = 255;
	}

	/*!
	 * Parses a heatmap report.
	 *
	 * This report contains the actual heatmap data. IPTS sends
	 * heatmaps "inverted", e.g. a contact is represented by a low
	 * value, and no contact is represented by a high value.
	 *
	 * After the data was parsed, the @ref on_heatmap callback will be invoked.
	 *
	 * @param[in] reader The chunk of data allocated to the report.
	 */
	void parse_heatmap_data(Reader &reader) const
	{
		Heatmap heatmap {};

		const usize size = casts::to<usize>(m_dim.width) * m_dim.height;

		heatmap.data = reader.subspan<u8>(size);
		heatmap.dim = m_dim;

		if (this->on_heatmap)
			this->on_heatmap(heatmap);
	}

	/*!
	 * Parses a heatmap frame.
	 *
	 * On HID-native devices, the heatmap is not passed as a report. Instead it is passed
	 * inside of a HID frame with a custom frame and header.
	 *
	 * @param[in] reader The chunk of data allocated to the frame.
	 */
	void parse_heatmap_frame(Reader &reader) const
	{
		const auto header = reader.read<struct ipts_heatmap_header>();
		Reader sub = reader.sub(header.size);

		this->parse_heatmap_data(sub);
	}

	/*!
	 * Parses a DFT window report.
	 *
	 * HID-native devices use DFT based pens. Instead of doing processing in firmware,
	 * and returning readily usable coordinates, these devices will return a set of
	 * antenna measurements and leave it to the client to determine the exact position
	 * of the stylus.
	 *
	 * After the data was parsed, the @ref on_dft callback will be invoked.
	 *
	 * @param[in] reader The chunk of data allocated to the report.
	 */
	void parse_dft_window(Reader &reader) const
	{
		DftWindow dft {};
		const auto window = reader.read<struct ipts_pen_dft_window>();


		for (usize i = 0; i < window.num_rows; i++)
			dft.x.at(i) = reader.read<struct ipts_pen_dft_window_row>();

		for (usize i = 0; i < window.num_rows; i++)
			dft.y.at(i) = reader.read<struct ipts_pen_dft_window_row>();

		dft.rows = window.num_rows;
		dft.type = window.data_type;

		if (window.seq_num == m_pen_meta.seq_num &&
		    window.data_type == m_pen_meta.data_type) {
			dft.group = casts::unpack(m_pen_meta.group_counter);
		}

		dft.dim = m_dim;

		if (!this->on_dft)
			return;

		this->on_dft(dft);
	}

	/*!
	 * Parses a pen metadata report.
	 *
	 * A pen metadata report precedes each DFT report.
	 *
	 * @param[in] reader The chunk of data allocated to the report.
	 */
	void parse_pen_metadata(Reader &reader)
	{
		m_pen_meta = reader.read<struct ipts_pen_metadata>();
	}
};

} // namespace iptsd::ipts

#endif // IPTSD_IPTS_PARSER_HPP
