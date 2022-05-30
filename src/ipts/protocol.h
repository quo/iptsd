/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IPTSD_IPTS_PROTOCOL_H
#define IPTSD_IPTS_PROTOCOL_H

#include <stdint.h>

#define IPTS_DATA_TYPE_PAYLOAD      0x0
#define IPTS_DATA_TYPE_ERROR        0x1
#define IPTS_DATA_TYPE_VENDOR_DATA  0x2
#define IPTS_DATA_TYPE_HID_REPORT   0x3
#define IPTS_DATA_TYPE_GET_FEATURES 0x4

#define IPTS_PAYLOAD_FRAME_TYPE_STYLUS  0x6
#define IPTS_PAYLOAD_FRAME_TYPE_HEATMAP 0x8

#define IPTS_REPORT_TYPE_START 0
#define IPTS_REPORT_TYPE_END   0xff

#define IPTS_REPORT_TYPE_HEATMAP_DIM  0x03
#define IPTS_REPORT_TYPE_HEATMAP      0x25
#define IPTS_REPORT_TYPE_STYLUS_V1    0x10
#define IPTS_REPORT_TYPE_STYLUS_V2    0x60

#define IPTS_REPORT_TYPE_FREQUENCY_NOISE          0x04
#define IPTS_REPORT_TYPE_PEN_GENERAL              0x57
#define IPTS_REPORT_TYPE_PEN_JNR_OUTPUT           0x58
#define IPTS_REPORT_TYPE_PEN_NOISE_METRICS_OUTPUT 0x59
#define IPTS_REPORT_TYPE_PEN_DATA_SELECTION       0x5a
#define IPTS_REPORT_TYPE_PEN_MAGNITUDE            0x5b
#define IPTS_REPORT_TYPE_PEN_DFT_WINDOW           0x5c
#define IPTS_REPORT_TYPE_PEN_MULTIPLE_REGION      0x5d
#define IPTS_REPORT_TYPE_PEN_TOUCHED_ANTENNAS     0x5e
#define IPTS_REPORT_TYPE_PEN_METADATA             0x5f
#define IPTS_REPORT_TYPE_PEN_DETECTION            0x62
#define IPTS_REPORT_TYPE_PEN_LIFT                 0x63

#define IPTS_STYLUS_REPORT_MODE_BIT_PROXIMITY 0
#define IPTS_STYLUS_REPORT_MODE_BIT_CONTACT   1
#define IPTS_STYLUS_REPORT_MODE_BIT_BUTTON    2
#define IPTS_STYLUS_REPORT_MODE_BIT_RUBBER    3

// FIXME should get these report IDs by parsing report descriptor and looking for digitizer usage 0x61
#define IPTS_HID_REPORT_IS_CONTAINER(x) (x==7 || x==8 || x==10 || x==11 || x==12 || x==13 || x==26 || x==28)
#define IPTS_HID_REPORT_SINGLETOUCH 0x40

#define IPTS_SINGLETOUCH_MAX_VALUE (1 << 15)

#define IPTS_MAX_X    9600
#define IPTS_MAX_Y    7200
#define IPTS_DIAGONAL 12000
#define IPTS_MAX_PRESSURE 4096

#define IPTS_DFT_NUM_COMPONENTS   9
#define IPTS_DFT_MAX_ROWS         16
#define IPTS_DFT_PRESSURE_ROWS    6

#define IPTS_DFT_ID_POSITION      6
#define IPTS_DFT_ID_BUTTON        9
#define IPTS_DFT_ID_PRESSURE      11

struct ipts_data {
	uint32_t type;
	uint32_t size;
	uint32_t buffer;
	uint8_t reserved[52];
} __attribute__((__packed__));

struct ipts_payload {
	uint32_t counter;
	uint32_t frames;
	uint8_t reserved[4];
} __attribute__((__packed__));

struct ipts_payload_frame {
	uint16_t index;
	uint16_t type;
	uint32_t size;
	uint8_t reserved[8];
} __attribute__((__packed__));

struct ipts_report {
	uint8_t type;
	uint8_t flags;
	uint16_t size;
} __attribute__((__packed__));

struct ipts_stylus_report {
	uint8_t elements;
	uint8_t reserved[3];
	uint32_t serial;
} __attribute__((__packed__));

struct ipts_stylus_data_v2 {
	uint16_t timestamp;
	uint16_t mode;
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
	uint16_t altitude;
	uint16_t azimuth;
	uint8_t reserved[2];
} __attribute__((__packed__));

struct ipts_stylus_data_v1 {
	uint8_t reserved[4];
	uint8_t mode;
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
	uint8_t reserved2;
} __attribute__((__packed__));

struct ipts_singletouch_data {
	uint8_t touch;
	uint16_t x;
	uint16_t y;
} __attribute__((__packed__));

struct ipts_heatmap_dim {
	uint8_t height;
	uint8_t width;
	uint8_t y_min;
	uint8_t y_max;
	uint8_t x_min;
	uint8_t x_max;
	uint8_t z_min;
	uint8_t z_max;
} __attribute__((__packed__));

struct ipts_report_start {
	uint8_t reserved[2];
	uint16_t count;
	uint32_t timestamp;
} __attribute__((__packed__));

struct ipts_hid_container {
	uint32_t size;
	uint8_t zero;    // always zero
	uint8_t type;    // 0 for root level container, 1 for heatmap container, 0xff for report container
	uint8_t unknown; // 1 for heatmap container, 0 for other containers
} __attribute__((__packed__));

struct ipts_hid_heatmap {
	uint8_t unknown1;  // always 8
	uint32_t unknown2; // always 0
	uint32_t size;
} __attribute__((__packed__));

struct ipts_pen_magnitude_data {
	uint8_t unknown1[2]; // always zero
	uint8_t unknown2[2]; // 0 if pen not near screen, 1 or 2 if pen is near screen
	uint8_t flags;       // 0, 1 or 8 (bitflags?)
	uint8_t unknown3[3]; // always 0xff (padding?)
	uint32_t x[64];
	uint32_t y[44];
};

struct ipts_pen_dft_window {
	uint32_t timestamp; // counting at approx 8MHz
	uint8_t num_rows;
	uint8_t seq_num;
	uint8_t unknown1;   // usually 1, can be 0 if there are simultaneous touch events
	uint8_t unknown2;   // usually 1, can be 0 if there are simultaneous touch events
	uint8_t unknown3;   // usually 1, but can be higher (2,3,4) for the first few packets of a pen interaction
	uint8_t data_type;
	uint16_t unknown4;  // always 0xffff (padding?)
};

struct ipts_pen_dft_window_row {
	uint32_t frequency;
	uint32_t magnitude;
	int16_t real[IPTS_DFT_NUM_COMPONENTS];
	int16_t imag[IPTS_DFT_NUM_COMPONENTS];
	int8_t first, last, mid, zero;
};

#endif /* IPTSD_IPTS_PROTOCOL_H */
