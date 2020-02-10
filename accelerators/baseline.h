#pragma once

#include "shape.h"

namespace fcpw {

template <int DIM>
class Baseline: public Aggregate<DIM> {
public:
	// constructor
	Baseline(const std::vector<std::shared_ptr<Shape<DIM>>>& shapes_);

	// returns bounding box
	BoundingBox<DIM> boundingBox() const;

	// returns centroid
	Vector<DIM> centroid() const;

	// returns surface area
	float surfaceArea() const;

	// returns signed volume
	float signedVolume() const;

	// intersects with ray
	int intersect(Ray<DIM>& r, std::vector<Interaction<DIM>>& is,
				  bool checkOcclusion=false, bool countHits=false,
				  bool collectAll=false) const;

	// finds closest point to sphere center
	bool findClosestPoint(BoundingSphere<DIM>& s, Interaction<DIM>& i) const;

private:
	// members
	const std::vector<std::shared_ptr<Shape<DIM>>>& shapes;
};

} // namespace fcpw

#include "baseline.inl"
