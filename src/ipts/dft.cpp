// SPDX-License-Identifier: GPL-2.0-or-later

#include "parser.hpp"

// calibration parameters
#define IPTS_DFT_POSITION_MIN_AMP 50
#define IPTS_DFT_POSITION_MIN_MAG 2000
#define IPTS_DFT_BUTTON_MIN_MAG   1000
#define IPTS_DFT_FREQ_MIN_MAG     10000
#define IPTS_DFT_POSITION_EXP     -.7 // tune this value to minimize jagginess of diagonal lines

namespace iptsd::ipts {

// TODO move dft/stylus processing to a separate class

static double dft_interpolate_position(const struct ipts_pen_dft_window_row *r)
{
	// assume the center component has the max amplitude
	unsigned maxi = IPTS_DFT_NUM_COMPONENTS/2;

	// off-screen components are always zero, don't use them
	double mind = -.5, maxd = .5;
	if      (r->real[maxi-1] == 0 && r->imag[maxi-1] == 0) { maxi++; mind = -1; }
	else if (r->real[maxi+1] == 0 && r->imag[maxi+1] == 0) { maxi--; maxd =  1; }

	// get phase-aligned amplitudes of the three center components
	double amp = sqrt(r->real[maxi]*r->real[maxi] + r->imag[maxi]*r->imag[maxi]);
	if (amp < IPTS_DFT_POSITION_MIN_AMP) return NAN;
	double sin = r->real[maxi] / amp, cos = r->imag[maxi] / amp;
	double x[] = {
		sin * r->real[maxi-1] + cos * r->imag[maxi-1],
		amp,
		sin * r->real[maxi+1] + cos * r->imag[maxi+1],
	};

	// convert the amplitudes into something we can fit a parabola to
	for (unsigned i = 0; i < 3; i++) x[i] = pow(x[i], IPTS_DFT_POSITION_EXP);

	// check orientation of fitted parabola
	if (x[0] + x[2] <= 2*x[1]) return NAN;

	// find critical point of fitted parabola
	double d = (x[0] - x[2]) / (2 * (x[0] - 2*x[1] + x[2]));

	return r->first + maxi + std::clamp(d, mind, maxd);
}

static double dft_interpolate_frequency(const struct ipts_pen_dft_window_row **x, const struct ipts_pen_dft_window_row **y, unsigned n)
{
	if (n < 3) return NAN;

	// find max row
	unsigned maxi = 0, maxm = 0;
	for (unsigned i = 0; i < n; i++) {
		unsigned m = x[i]->magnitude + y[i]->magnitude;
		if (m > maxm) { maxm = m; maxi = i; }
	}
	if (maxm < 2*IPTS_DFT_FREQ_MIN_MAG) return NAN;

	double mind = -.5, maxd = .5;
	if      (maxi <   1) { maxi =   1; mind = -1; }
	else if (maxi > n-2) { maxi = n-2; maxd =  1; }

	// all components in a row have the same phase, and corresponding x and y rows also have the same phase, so we can add everything together
	int real[3], imag[3];
	for (unsigned i = 0; i < 3; i++) {
		real[i] = imag[i] = 0;
		for (unsigned j = 0; j < IPTS_DFT_NUM_COMPONENTS; j++) {
			real[i] += x[maxi+i-1]->real[j] + y[maxi+i-1]->real[j];
			imag[i] += x[maxi+i-1]->imag[j] + y[maxi+i-1]->imag[j];
		}
	}

	// interpolate using Eric Jacobsen's modified quadratic estimator
	int ra = real[0] - real[2], rb = 2*real[1] - real[0] - real[2];
	int ia = imag[0] - imag[2], ib = 2*imag[1] - imag[0] - imag[2];
	double d = (ra*rb + ia*ib) / (double)(rb*rb + ib*ib);

	return (maxi + std::clamp(d, mind, maxd)) / (n-1);
}

void Parser::stop_stylus()
{
	if (stylus.proximity) {
		stylus.proximity = false;
		stylus.contact = false;
		stylus.button = false;
		stylus.rubber = false;
		stylus.pressure = 0;
		if (on_stylus) on_stylus(stylus);
	}
}

void Parser::process_dft(const struct ipts_pen_dft_window &dft,
	const struct ipts_pen_dft_window_row **dft_x,
	const struct ipts_pen_dft_window_row **dft_y)
{
	switch (dft.data_type) {

	case IPTS_DFT_ID_POSITION:
		if (dft.num_rows > 0 && num_cols && num_rows
			&& dft_x[0]->magnitude > IPTS_DFT_POSITION_MIN_MAG
			&& dft_y[0]->magnitude > IPTS_DFT_POSITION_MIN_MAG)
		{
			stylus_real = dft_x[0]->real[IPTS_DFT_NUM_COMPONENTS/2] + dft_y[0]->real[IPTS_DFT_NUM_COMPONENTS/2];
			stylus_imag = dft_x[0]->imag[IPTS_DFT_NUM_COMPONENTS/2] + dft_y[0]->imag[IPTS_DFT_NUM_COMPONENTS/2];
			double x = dft_interpolate_position(dft_x[0]);
			double y = dft_interpolate_position(dft_y[0]);
			bool prox = !std::isnan(x) && !std::isnan(y);
			if (prox) {
				stylus.proximity = true;
				x /= num_cols-1;
				y /= num_rows-1;
				if (invert_x) x = 1-x;
				if (invert_y) y = 1-y;
				stylus.x = (uint16_t)(std::clamp(x, 0., 1.) * IPTS_MAX_X + .5);
				stylus.y = (uint16_t)(std::clamp(y, 0., 1.) * IPTS_MAX_Y + .5);
				if (on_stylus) on_stylus(stylus);
			} else stop_stylus();
		} else stop_stylus();
		break;

	case IPTS_DFT_ID_BUTTON:
		if (dft.num_rows > 0) {
			bool rubber;
			if (dft_x[0]->magnitude > IPTS_DFT_BUTTON_MIN_MAG && dft_y[0]->magnitude > IPTS_DFT_BUTTON_MIN_MAG) {
				// same phase as position signal = eraser, opposite phase = button
				int btn = stylus_real * (dft_x[0]->real[IPTS_DFT_NUM_COMPONENTS/2] + dft_y[0]->real[IPTS_DFT_NUM_COMPONENTS/2])
				        + stylus_imag * (dft_x[0]->imag[IPTS_DFT_NUM_COMPONENTS/2] + dft_y[0]->imag[IPTS_DFT_NUM_COMPONENTS/2]);
				stylus.button = btn < 0;
				rubber = btn > 0;
			} else {
				stylus.button = false;
				rubber = false;
			}
			// toggling rubber while proximity is true seems to cause issues, so set proximity off first
			if (rubber != stylus.rubber) stop_stylus();
			stylus.rubber = rubber;
		}
		break;

	case IPTS_DFT_ID_PRESSURE:
		if (dft.num_rows >= IPTS_DFT_PRESSURE_ROWS) {
			double p = dft_interpolate_frequency(dft_x, dft_y, IPTS_DFT_PRESSURE_ROWS);
			p = (1 - p) * IPTS_MAX_PRESSURE;
			if (p > 1) {
				stylus.contact = true;
				stylus.pressure = std::min(IPTS_MAX_PRESSURE, (int)p);
			} else {
				stylus.contact = false;
				stylus.pressure = 0;
			}
		}
		break;

	}
}

}

