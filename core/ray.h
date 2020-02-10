#pragma once

#include "core.h"

namespace fcpw {

template <int DIM>
class Ray {
public:
	// constructor
	Ray(const Vector<DIM>& o_, const Vector<DIM>& d_, float tMax_=maxFloat):
		o(o_), d(d_), tMax(tMax_) {
		for (int i = 0; i < DIM; i++) {
			invD(i) = 1.0f/d(i);
		}
	}

	// operator()
	Vector<DIM> operator()(float t) const {
		return o + d*t;
	}

	// computes transformed ray
	Ray<DIM> transform(const Transform<float, DIM, Affine>& t) const {
		Vector<DIM> to = t*o;
		Vector<DIM> td = t*(o + (tMax < maxFloat ? tMax : 1.0f)*d) - to;
		float tdNorm = td.norm();

		return Ray<DIM>(to, td/tdNorm, tMax < maxFloat ? tdNorm : maxFloat);
	}

	// members
	Vector<DIM> o, d, invD;
	float tMax;
};

} // namespace fcpw
