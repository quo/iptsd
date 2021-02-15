// SPDX-License-Identifier: GPL-2.0-or-later

#include "touch-processing.hpp"

#include "cone.hpp"
#include "config.hpp"
#include "contact.hpp"
#include "finger.hpp"
#include "heatmap.hpp"

#include <ipts/ipts.h>
#include <ipts/protocol.h>

#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>

f64 TouchInput::dist(TouchInput o)
{
	f64 dx = (f64)this->x - (f64)o.x;
	f64 dy = (f64)this->y - (f64)o.y;

	return std::sqrt(dx * dx + dy * dy);
}

void TouchInput::reset(void)
{
	this->x = 0;
	this->y = 0;
	this->major = 0;
	this->minor = 0;
	this->orientation = 0;
	this->ev1 = 0;
	this->ev2 = 0;
	this->index = -1;
	this->is_stable = false;
	this->is_palm = false;
}

TouchProcessor::TouchProcessor(struct ipts_device_info info, IptsdConfig *conf)
{
	this->info = info;
	this->config = conf;

	this->contacts = std::vector<Contact>(info.max_contacts);
	this->inputs = std::vector<TouchInput>(info.max_contacts);
	this->last = std::vector<TouchInput>(info.max_contacts);
	this->free_indices = std::vector<bool>(info.max_contacts);
	this->distances = std::vector<f64>(info.max_contacts * info.max_contacts);
	this->indices = std::vector<size_t>(info.max_contacts * info.max_contacts);

	for (size_t i = 0; i < std::size(this->last); i++) {
		this->last[i].reset();
		this->last[i].slot = i;
		this->last[i].contact = &this->contacts[i];
		this->free_indices[i] = true;
	}
}

void TouchProcessor::process(Heatmap *hm)
{
	f32 average = hm->average();

	for (size_t i = 0; i < hm->size; i++) {
		if (hm->data[i] < average)
			hm->data[i] = average - hm->data[i];
		else
			hm->data[i] = 0;
	}

	size_t count = Contact::find_contacts(hm, this->contacts);

	for (size_t i = 0; i < count; i++) {
		f32 x = this->contacts[i].x / (hm->width - 1);
		f32 y = this->contacts[i].y / (hm->height - 1);

		if (this->config->invert_x)
			x = 1 - x;

		if (this->config->invert_y)
			y = 1 - y;

		this->contacts[i].x = x * this->config->width;
		this->contacts[i].y = y * this->config->height;
	}

	this->find_palms(count);

	for (size_t i = 0; i < count; i++) {
		f32 x = this->contacts[i].x / this->config->width;
		f32 y = this->contacts[i].y / this->config->height;

		// ev1 is always the larger eigenvalue.
		f32 orientation = this->contacts[i].angle / M_PI * 180;
		f32 maj = 4 * std::sqrt(this->contacts[i].ev1) / hm->diagonal;
		f32 min = 4 * std::sqrt(this->contacts[i].ev2) / hm->diagonal;

		this->inputs[i].x = (i32)(x * IPTS_MAX_X);
		this->inputs[i].y = (i32)(y * IPTS_MAX_Y);
		this->inputs[i].major = (i32)(maj * IPTS_DIAGONAL);
		this->inputs[i].minor = (i32)(min * IPTS_DIAGONAL);
		this->inputs[i].orientation = (i32)orientation;
		this->inputs[i].ev1 = this->contacts[i].ev1;
		this->inputs[i].ev2 = this->contacts[i].ev2;
		this->inputs[i].index = i;
		this->inputs[i].slot = i;
		this->inputs[i].is_stable = false;
		this->inputs[i].is_palm = this->contacts[i].is_palm;
		this->inputs[i].contact = &this->contacts[i];
	}

	for (size_t i = count; i < this->info.max_contacts; i++) {
		this->inputs[i].reset();
		this->inputs[i].slot = i;
		this->inputs[i].contact = &this->contacts[i];
	}

	iptsd_finger_track(this, count);
	this->save();
}

void TouchProcessor::save(void)
{
	for (size_t i = 0; i < std::size(this->free_indices); i++)
		this->free_indices[i] = true;

	for (size_t i = 0; i < std::size(this->inputs); i++)
		this->last[i] = this->inputs[i];
}

void TouchProcessor::update_cone(Contact *palm)
{
	Cone *cone = nullptr;
	f32 d = INFINITY;

	// find closest cone (by center)
	for (Cone *current : this->rejection_cones) {
		// This cone has never seen a position update, so its inactive
		if (!current->was_active())
			continue;

		if (current->is_removed())
			continue;

		f32 current_d = current->hypot(palm->x, palm->y);

		if (current_d < d) {
			d = current_d;
			cone = current;
		}
	}

	if (!cone)
		return;

	cone->update_direction(palm->x, palm->y);
}

bool TouchProcessor::check_cone(Contact *input)
{
	for (Cone *cone : this->rejection_cones) {
		if (cone->is_inside(input->x, input->y))
			return true;
	}

	return false;
}

void TouchProcessor::find_palms(size_t count)
{
	for (size_t i = 0; i < count; i++) {
		f32 vx = this->contacts[i].ev1;
		f32 vy = this->contacts[i].ev2;
		f32 max_v = this->contacts[i].max_v;

		// Regular touch
		if (vx < 0.6 || (vx < 1.0 && max_v > 80))
			continue;

		// Thumb
		if ((vx < 1.25 || (vx < 3.5 && max_v > 90)) && vx / vy > 1.8)
			continue;

		this->contacts[i].is_palm = true;
		this->update_cone(&this->contacts[i]);

		for (size_t j = 0; j < count; j++) {
			if (this->contacts[j].is_palm)
				continue;

			if (this->contacts[j].near(this->contacts[i]))
				this->contacts[j].is_palm = true;
		}
	}

	for (size_t i = 0; i < count; i++) {
		if (this->contacts[i].is_palm)
			continue;

		if (this->check_cone(&this->contacts[i]))
			this->contacts[i].is_palm = true;
	}
}