// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef IPTSD_CONTACTS_DETECTION_ALGORITHMS_ELLIPSE_HPP
#define IPTSD_CONTACTS_DETECTION_ALGORITHMS_ELLIPSE_HPP

#include <common/types.hpp>

#include <cmath>

namespace iptsd::contacts::detection::ellipse {

/*!
 * Calculates the size of an ellipse.
 *
 * @param[in] eigenvalues The eigenvalues of the ellipse.
 * @return The diameter of both axes of the ellipse.
 */
template <class T> Vector2<T> size(const Vector2<T> &eigenvalues)
{
	const Vector2<T> size = eigenvalues.cwiseAbs().cwiseSqrt();

	// The eigenvalues are the radius of the ellipse but we want to return the diameter.
	return (size.array() * static_cast<T>(2)).matrix();
}

/*!
 * Calculates the orientation of an ellipse.
 *
 * @param[in] eigenvectors The eigenvectors of the ellipse.
 * @return The orientation of the ellipse in radians.
 */
template <class T> T angle(const Matrix2<T> &eigenvectors)
{
	constexpr auto PI = static_cast<T>(M_PIf);

	const Vector2<T> ev1 = eigenvectors.col(0);
	const T angle = std::atan2(ev1.x(), ev1.y()) + (PI / 2);

	/*
	 * It is not possible to say if the contact faces up or down,
	 * so we make sure the angle is between 0° and 180° to be consistent.
	 */
	if (angle < 0)
		return angle + PI;
	else if (angle >= PI)
		return angle - PI;
	else
		return angle;
}

} // namespace iptsd::contacts::detection::ellipse

#endif // IPTSD_CONTACTS_DETECTION_ALGORITHMS_ELLIPSE_HPP