#pragma once

#include <fcpw/core/primitive.h>
#include <map>

namespace fcpw {

template<size_t DIM>
struct PolygonSoup {
	// constructor
	PolygonSoup() {}

	// constructor
 	PolygonSoup(const std::vector<int>& indices_,
				const std::vector<Vector<DIM>>& positions_):
				indices(indices_), positions(positions_) {}

	// members
	std::vector<int> indices /* a.k.a. vIndices */, eIndices, tIndices;
	std::vector<Vector<DIM>> positions;
	std::vector<Vector<DIM - 1>> textureCoordinates;
	std::vector<Vector<DIM>> vNormals, eNormals; // normalized values
	
	// edge to face, vertex to face mapping data structures
	int edgeIndexOffset;
	int vertexIndexOffset;
	std::vector<int> faceIndexBufferOffsets;
	std::vector<int> faceIndexBuffer;

	// temporary datastructures, build() sorts vertices.
	// therefore, we keep this data in order to rebuild the
	// faceIndexBufferOffsets and faceIndexBuffer data structures.
	// these maps are cleared after sorting.
	std::map<int, std::vector<int>> vertexIdToFacesMap;
	std::map<int, std::vector<int>> edgeIdToFacesMap;
};

} // namespace fcpw
