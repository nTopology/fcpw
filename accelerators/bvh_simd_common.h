#pragma once

#include "immintrin.h"
#include <array>
#include "bounding_volumes.h"
#include "simd.h"

namespace fcpw{

    // TEMP NOTE: prep for simd optimized bvhs
    template <int DIM, int W>
    struct BvhSimdFlatNode{
        // pMin first then pMax
        // below is SSE example
        // x1 x2 x3 x4
        // y1 y2 y3 y4
        // ...
        using SimdType = typename IntrinsicType<W>::type;
        // using CounterType = typename CountType<W>::type;
        
        SimdType minBoxes[DIM];
        SimdType maxBoxes[DIM];
        int indices[W];
        Vector<DIM> centroid;
        char sortOrder[DIM][W];
        bool isLeaf[W];

        // constructor
        BvhSimdFlatNode(){
            minBoxes = vecZero<W>().vec;
            maxBoxes = vecZero<W>().vec;
            for(int i = 0; i < W; i++){
                indices[i] = -1;
                isLeaf[i] = false;
            }
            for(char i = 0; i < DIM; i++)
            for(char j = 0; j < W; j++){
                sortOrder[i][j] = j;
            }
        }

        // gets ordering based on dimension and iterator position
        __forceinline char getOrdering(char dim, char x) const{
            return sortOrder[dim][x];
        }

        // adds a bounding box to this node at index in node
        inline void addBounds(const Vector<DIM>& pMin, const Vector<DIM>& pMax, int index){
            for(int i = 0; i < DIM; i++){
                minBoxes[i][index] = pMin(i);
                maxBoxes[i][index] = pMax(i);
            }
        }

        // sets presorted order based on all 3 axes
        inline void setSortOrder(){
            float midpoints[W];
            for(int i = 0; i < DIM; i++){
                for(int j = 0; indices[j] != -1 && j < W; j++){
                    midpoints[j] = (minBoxes[i][j] + maxBoxes[i][j]) * .5;
                }
                bool isSorted = false;
                while(!isSorted){
                    isSorted = true;
                    for(int j = 0; indices[j] != -1 && j < W - 1; j++){
                        if(midpoints[j] > midpoints[j + 1]){
                            isSorted = false;
                            std::swap(midpoints[j], midpoints[j + 1]);
                            std::swap(sortOrder[i][j], sortOrder[i][j + 1]);
                        }
                    }
                }
            }
        }
    };

    // template partial specialization to use Embree vectors (over just structure of arrays)
    template <int W>
    struct BvhSimdFlatNode<3, W>{
        simdBox_type<W> boxes;
        int indices[W];
        Vector<3> centroid;
        char sortOrder[3][W];
        bool isLeaf[W];

        // constructor
        BvhSimdFlatNode(){
            boxes[0] = embree::Vec3<simdFloat<W>>(vecZero<W>(), vecZero<W>(), vecZero<W>());
            boxes[1] = embree::Vec3<simdFloat<W>>(vecZero<W>(), vecZero<W>(), vecZero<W>());
            for(int i = 0; i < W; i++){
                indices[i] = -1;
                isLeaf[i] = false;
            }
            for(char i = 0; i < 3; i++)
            for(char j = 0; j < W; j++){
                sortOrder[i][j] = j;
            }
            // junk code, ordering based on bitvectors (maybe revisit?)
            // for(int i = 0; i < 3; i++){
            //     sortOrder[i] = 27;
            // }
        }

        __forceinline char getOrdering(char dim, char x) const{
            return sortOrder[dim][x];
        }

        inline void addBounds(const Vector<3>& pMin, const Vector<3>& pMax, int index){
            for(int i = 0; i < 3; i++){
                boxes[0][i].vec[index] = pMin(i);
                boxes[1][i].vec[index] = pMax(i);
            }
        }

        inline void setSortOrder(){
            float midpoints[W];
            for(int i = 0; i < 3; i++){
                for(int j = 0; indices[j] != -1 && j < W; j++){
                    midpoints[j] = (boxes[0][i][j] + boxes[1][i][j]) * .5;
                }
                bool isSorted = false;
                while(!isSorted){
                    isSorted = true;
                    for(int j = 0; indices[j] != -1 && j < W - 1; j++){
                        if(midpoints[j] > midpoints[j + 1]){
                            isSorted = false;
                            std::swap(midpoints[j], midpoints[j + 1]);
                            std::swap(sortOrder[i][j], sortOrder[i][j + 1]);
                        }
                    }
                }
            }
        }
    };

    // triangles only bvh simd leaf node
    template <int DIM, int W>
    struct BvhSimdLeafNode{
        using SimdType = typename IntrinsicType<W>::type;

        // SimdType minBoxes[DIM];
        // SimdType maxBoxes[DIM];
        SimdType pa[DIM];
        SimdType pb[DIM]; 
        SimdType pc[DIM];
        int indices[W]; // will later wrap in triangle data

        // initializes triangle points
        inline void initPoints(float pointCoords[3][DIM][W]){
            for(int i = 0; i < DIM; i++){
                initSimd(pointCoords[0][i], pa[i]);
                initSimd(pointCoords[1][i], pb[i]);
                initSimd(pointCoords[2][i], pc[i]);
            }
        }
    };

    // template partial specialization for embree vectors
    template <int W>
    struct BvhSimdLeafNode<3, W>{
        using SimdType = typename IntrinsicType<W>::type;
        simdTriangle_type<W> triangles;
        int indices[W];

        // initializes triangle points
        inline void initPoints(float pointCoords[3][3][W]){
            for(int i = 0; i < 3; i++){
                SimdType px, py, pz;
                initSimd(pointCoords[i][0], px);
                initSimd(pointCoords[i][1], py);
                initSimd(pointCoords[i][2], pz);
                triangles[i] = embree::Vec3<simdFloat<W>>(vecf<W>(px), vecf<W>(py), vecf<W>(pz));
            }
        }
    };

    // parallel bounding sphere
    template <int DIM, int W>
    struct SimdBoundingSphere{
        using SimdType = typename IntrinsicType<W>::type;
        // constructor
        SimdBoundingSphere(const Vector<DIM>& c_, float r2_){
            for(int i = 0; i < DIM; i++){
                initSimd(c_(i), c[i]);
            }
            initSimd(r2_, r2);
        }
        SimdBoundingSphere(BoundingSphere<DIM>& s){
            for(int i = 0; i < DIM; i++){
                initSimd(s.c(i), c[i]);
            }
            initSimd(s.r2, r2);
        }

        inline void getMinRad(){
            float minRad = maxFloat;
            for(int i = 0; i < W; i++){
                if(r2[i] < minRad){
                    minRad = r2[i];
                }
            }
        }

        inline void setRad(float rad2){
            initSimd(rad2, r2);
        }

        SimdType c[DIM];
        SimdType r2;
    };

    // embree parallel bounding sphere
    template <int W>
    struct SimdBoundingSphere<3, W>{
        using SimdType = typename IntrinsicType<W>::type;
        // constructor
        SimdBoundingSphere(const Vector<3>& c_, float r2_){
            SimdType px, py, pz, r;
            initSimd(c_(0), px);
            initSimd(c_(1), py);
            initSimd(c_(2), pz);
            c = embree::Vec3<simdFloat<W>>(vecf<W>(px),vecf<W>(py),vecf<W>(pz));
            initSimd(r2_, r);
            r2 = vecf<W>(r);
        }
        SimdBoundingSphere(BoundingSphere<3>& s){
            SimdType px, py, pz, r;
            initSimd(s.c(0), px);
            initSimd(s.c(1), py);
            initSimd(s.c(2), pz);
            c = embree::Vec3<simdFloat<W>>(vecf<W>(px),vecf<W>(py),vecf<W>(pz));
            initSimd(s.r2, r);
            r2 = vecf<W>(r);
        }

        inline void getMinRad(){
            float minRad = maxFloat;
            for(int i = 0; i < W; i++){
                if(r2.vec[i] < minRad){
                    minRad = r2.vec[i];
                }
            }
        }

        inline void setRad(float rad2){
            SimdType sr2;
            initSimd(rad2, sr2);
            r2 = vecf<W>(sr2);
        }

        simdPoint_type<W> c;
        simdFloat<W> r2;
    };

    // parallel overlap results with bounding boxes
    template <int W>
    struct ParallelOverlapResult{
        simdFloat<W> d2Min;
        simdFloat<W> d2Max;
    };

    // parallel triangle closest point results
    template <int DIM, int W>
    struct ParallelInteraction{
        using SimdType = typename IntrinsicType<W>::type;
        SimdType distances;
        SimdType points[DIM];
        int indices[W];

        void getBest(float& distance, float point[DIM], int& index){
            int loc = 0;
            distance = distances[0];
            for(int i = 1; indices[i] != -1 && i < W; i++){
                if(distance > distances[i]){
                    distance = distances[i];
                    loc = i;
                }
            }
            index = indices[loc];
            for(int j = 0; j < DIM; j++){
                point[j] = points[j][loc];
            }
        }
    };

    // parallel triangle closest point results using embree vectors
    template <int W>
    struct ParallelInteraction<3, W>{
        simdFloat<W> distances;
        simdFloatVec<W> points;
        int indices[W];

        void getBest(float& distance, float point[3], int& index){
            // #ifdef PROFILE
            //     PROFILE_SCOPED();
            // #endif
            distance = distances.vec[0];
            index = indices[0];
            for(int i = 0; i < 3; i++){
                point[i] = points[i].vec[0];
            }
            for(int i = 1; indices[i] != -1 && i < W; i++){
                if(distance > distances.vec[i]){
                    distance = distances.vec[i];
                    index = indices[i];
                    for(int j = 0; j < 3; j++){
                        point[j] = points[j].vec[i];
                    }
                }
            }
        }
    };

    /* ---- Vectorized Functions ---- */

    // embree bounding sphere - AABB closest and furthest point computations
    template <int W>
    void simdBoxOverlap(simdFloat<W>& closestDistances, simdFloat<W>& furthestDistances, const simdPoint_type<W>& iPoint, const simdBox_type<W>& iBoxes){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
        closestDistances = length2(max(max(iBoxes[0] - iPoint, iPoint - iBoxes[1]), zeroVector<W>()));
        furthestDistances = length2(max(iPoint - iBoxes[0], iBoxes[1] - iPoint));
    }

    // wrapper for bounding sphere - AABB closest point function
    template <int DIM, int W>
    void parallelOverlap(const BvhSimdFlatNode<DIM, W>& node, SimdBoundingSphere<DIM, W>& sbs, ParallelOverlapResult<W>& result){        
        #ifdef PROFILE
            PROFILE_SCOPED();
        #endif
        LOG(FATAL) << "Not yet implemented for dimension " << DIM;
    }

    // function overloading for embree vectors
    template <int DIM, int W>
    void parallelOverlap(const BvhSimdFlatNode<3, W>& node, SimdBoundingSphere<3, W>& sbs, ParallelOverlapResult<W>& result){      
        #ifdef PROFILE
            PROFILE_SCOPED();
        #endif
        simdBoxOverlap(result.d2Min, result.d2Max, sbs.c, node.boxes);
    }

    // embree point - triangle closest point computation
    // NOTE: will need to somehow get the important info to get triangle normals here
    template <int W>
    simdFloat<W> simdTriPoint2(simdFloatVec<W>& oTriPoint, const simdTriangle_type<W>& iTri, const simdPoint_type<W>& iPoint){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
		// Check if P in vertex region outside A
		const simdFloatVec<W> ab = iTri[1] - iTri[0];
		const simdFloatVec<W> ac = iTri[2] - iTri[0];
		const simdFloatVec<W> ap = iPoint - iTri[0];
		const simdFloat<W> d1 = dot(ab, ap);
		const simdFloat<W> d2 = dot(ac, ap);
		const simdBool<W> mask1 = (d1 <= simdFloat<W>(vecZero<4>().vec)) & (d2 <= simdFloat<W>(vecZero<4>().vec));
		oTriPoint = iTri[0];
		simdBool<W> exit(mask1);
		if (all(exit))
			return length2(oTriPoint - iPoint);  // barycentric coordinates (1,0,0)

		// Check if P in vertex region outside B
		const simdFloatVec<W> bp = iPoint - iTri[1];
		const simdFloat<W> d3 = dot(ab, bp);
		const simdFloat<W> d4 = dot(ac, bp);
		const simdBool<W> mask2 = (d3 >= simdFloat<W>(vecZero<4>().vec)) & (d4 <= d3);
		exit |= mask2;
		oTriPoint = select(mask2, iTri[1], oTriPoint);
		if (all(exit))
			return length2(oTriPoint - iPoint);  // barycentric coordinates (0,1,0)

		// Check if P in vertex region outside C
		const simdFloatVec<W> cp = iPoint - iTri[2];
		const simdFloat<W> d5 = dot(ab, cp);
		const simdFloat<W> d6 = dot(ac, cp);
		const simdBool<W> mask3 = (d6 >= simdFloat<W>(vecZero<4>().vec)) & (d5 <= d6);
		exit |= mask3;
		oTriPoint = select(mask3, iTri[2], oTriPoint);
		if (all(exit))
			return length2(oTriPoint - iPoint);  // barycentric coordinates (0,0,1)

		// Check if P in edge region of AB, if so return projection of P onto AB
		const simdFloat<W> vc = d1*d4 - d3*d2;
		const simdBool<W> mask4 = (vc <= simdFloat<W>(vecZero<4>().vec)) & (d1 >= simdFloat<W>(vecZero<4>().vec)) & (d3 <= simdFloat<W>(vecZero<4>().vec));
		exit |= mask4;
		const simdFloat<W> v1 = d1 / (d1 - d3);
		const simdFloatVec<W> answer1 = iTri[0] + v1 * ab;
		oTriPoint = select(mask4, answer1, oTriPoint);
		if (all(exit))
			return length2(oTriPoint - iPoint);  // barycentric coordinates (1-v,v,0)

		// Check if P in edge region of AC, if so return projection of P onto AC
		const simdFloat<W> vb = d5*d2 - d1*d6;
		const simdBool<W> mask5 = (vb <= simdFloat<W>(vecZero<4>().vec)) & (d2 >= simdFloat<W>(vecZero<4>().vec)) & (d6 <= simdFloat<W>(vecZero<4>().vec));
		exit |= mask5;
		const simdFloat<W> w1 = d2 / (d2 - d6);
		const simdFloatVec<W> answer2 = iTri[0] + w1 * ac;
		oTriPoint = select(mask5, answer2, oTriPoint);
		if (all(exit))
			return length2(oTriPoint - iPoint);  // barycentric coordinates (1-w,0,w)

		// Check if P in edge region of BC, if so return projection of P onto BC
		const simdFloat<W> va = d3*d6 - d5*d4;
		const simdBool<W> mask6 = (va <= simdFloat<W>(vecZero<4>().vec)) & ((d4 - d3) >= simdFloat<W>(vecZero<4>().vec)) & ((d5 - d6) >= simdFloat<W>(vecZero<4>().vec));
		exit |= mask6;
		simdFloat<W> w2 = (d4 - d3) / ((d4 - d3) + (d5 - d6));
		const simdFloatVec<W> answer3 = iTri[1] + w2 * (iTri[2] - iTri[1]);
		oTriPoint = select(mask6, answer3, oTriPoint);
		if (all(exit))
			return length2(oTriPoint - iPoint); // barycentric coordinates (0,1-w,w)

		// P inside face region. Compute Q through its barycentric coordinates (u,v,w)
		const simdFloat<W> denom = simdFloat<W>(_mm_set1_ps(1)) / (va + vb + vc);
		const simdFloat<W> v2 = vb * denom;
		const simdFloat<W> w3 = vc * denom;
		const simdFloatVec<W> answer4 = iTri[0] + ab * v2 + ac * w3;
		const simdBool<W> mask7 = andnot(exit, length2(answer4 - iPoint) < length2(oTriPoint - iPoint));
		oTriPoint = select(mask7, answer4, oTriPoint);
		return length2(oTriPoint - iPoint);  // = u*a + v*b + w*c, u = va * denom = 1.0f - v - w
	}

    // wrapper for parallel triangle closest point
    template <int DIM, int W>
    void parallelTriangleClosestPoint(const BvhSimdLeafNode<DIM, W>& node, SimdBoundingSphere<DIM, W>& s, ParallelInteraction<DIM, W>& pi){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
        LOG(FATAL) << "Triangle closest point not available for dimension " << DIM << " and width " << W;
    }

    // embree triangle closest point
    template <int DIM, int W>
    void parallelTriangleClosestPoint(const BvhSimdLeafNode<3, W>& node, SimdBoundingSphere<3, W>& s, ParallelInteraction<3, W>& pi){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
        pi.distances = simdTriPoint2<W>(pi.points, node.triangles, s.c);
    }

} //namespace fcpw