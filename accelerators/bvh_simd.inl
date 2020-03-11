#include <stack>
#include <queue>
#include <chrono>

namespace fcpw{

    struct BvhSimdBuildNode{
        BvhSimdBuildNode(int nodeIndex_, int parentIndex_, int depth_=0) :
            nodeIndex(nodeIndex_), parentIndex(parentIndex_), depth(depth_){}
        int nodeIndex, parentIndex, depth;
    };

    template <int DIM, int W>
    inline BvhSimd<DIM, W>::BvhSimd(
        const std::vector<BvhFlatNode<DIM>>& nodes_,
        const std::vector<ReferenceWrapper<DIM>>& references_,
        const std::vector<std::shared_ptr<Primitive<DIM>>>& primitives_,
        const std::string& parentDescription_):
        nNodes(0), nLeaves(0), primitives(primitives_), bbox(nodes_[0].bbox),
        depth(0), nReferences(0), averageLeafSize(0), nPrimitives(primitives_.size()){
        
        // just here for me to check on node size for caching reasons (this machine has cache length of 64, too small for optimal flatnode)
        LOG(INFO) << "Size of interior node: " << sizeof(BvhSimdFlatNode<DIM, W>);
        LOG(INFO) << "Size of leaf node: " << sizeof(BvhSimdLeafNode<DIM, W>);

        // build and time build of mbvh
        std::chrono::high_resolution_clock::time_point t_start, t_end;
        std::chrono::nanoseconds duration;
        double buildTime = 0;

        t_start = std::chrono::high_resolution_clock::now();
        build(nodes_, references_);
        t_end = std::chrono::high_resolution_clock::now();
        duration = t_end - t_start;
        buildTime = (double)(duration.count()) / std::chrono::nanoseconds::period::den;

        // output mbvh data
        averageLeafSize /= nLeaves;

        std::string simdMethod;
        switch(W){
            case 4:
                simdMethod = "SSE";
                break;
            case 8:
                simdMethod = "AVX";
                break;
            case 16:
                simdMethod = "AVX512";
                break;
            default:
                simdMethod = "INVALID";
                break;
        }
        LOG(INFO) << simdMethod << " Bvh created with "
                    << nNodes << " nodes, "
                    << nLeaves << " leaves with average size " << averageLeafSize << ", "
                    << nPrimitives << " primitives, "
                    << depth << " depth, in "
                    << buildTime << " seconds, "
                    << parentDescription_;
    }

    template <int DIM, int W>
    inline void BvhSimd<DIM, W>::build(const std::vector<BvhFlatNode<DIM>>& nodes, const std::vector<ReferenceWrapper<DIM>>& references){
        // useful containers
        std::stack<BvhSimdBuildNode> todo;
        std::stack<BvhTraversalDepth> nodeWorkingSet;
        std::vector<BvhSimdFlatNode<DIM, W>> buildNodes;
        std::vector<BvhSimdLeafNode<DIM, W>> buildLeaves;

        // computes furthest depth from any given node to traverse depending on vector width
        int maxDepth = W == 4 ? 2 : (W == 8 ? 3 : (W == 16 ? 4 : 0));
        LOG_IF(FATAL, maxDepth == 0) << "BvhSimd::build(): Provided width for SIMD is invalid";

        // push root node
        todo.emplace(BvhSimdBuildNode(0, -1, 0));

        // NEED TO INTEGRATE TRAVERSAL ORDER SOMEHOW INTO THIS

        // process todo
        while(!todo.empty()){
            // pop off node for processing
            BvhSimdBuildNode toBuild = todo.top();
            todo.pop();

            // process build node
            int nodeIndex = toBuild.nodeIndex;
            int parentIndex = toBuild.parentIndex;
            if(depth < toBuild.depth) depth = toBuild.depth;
            int toBuildDepth = toBuild.depth;
            const BvhFlatNode<DIM>& curNode = nodes[nodeIndex];

            // construct a new flattened tree node
            if(parentIndex == -1 || curNode.rightOffset != 0){
                buildNodes.emplace_back(BvhSimdFlatNode<DIM, W>());
                BvhSimdFlatNode<DIM, W>& node = buildNodes.back();
                node.centroid = curNode.bbox.centroid();
                nNodes ++;
            }
            int simdTreeIndex = buildNodes.size() - 1;

            // fill node with data (if not root node)
            if(parentIndex != -1){
                // setup useful variables
                BvhSimdFlatNode<DIM, W>& parentNode = buildNodes[parentIndex];
                int tempCounter = 0;
                while(parentNode.indices[tempCounter] != -1){
                    tempCounter ++;
                    LOG_IF(FATAL, tempCounter >= W) << "Tempcounter out of bounds";
                }
                BoundingBox<DIM> bbox = curNode.bbox;

                // fill bbox data in node
                parentNode.addBounds(bbox.pMin, bbox.pMax, tempCounter);

                // connect popped off node to associated build node
                if(curNode.rightOffset == 0){
                    // if node is leaf, construct the parallel leaf, link and move on

                    averageLeafSize += (float)curNode.nPrimitives;
                    
                    // fill triangle vertex info and feed into new parallel leaf node
                    float tripoints[3][DIM][W];
                    nLeaves ++;
                    buildLeaves.emplace_back(BvhSimdLeafNode<DIM, W>());
                    BvhSimdLeafNode<DIM, W>& leafNode = buildLeaves.back();
                    for(int i = 0; i < W; i++){
                        // if in node with less than vector width amount of primitives, fill rest of vectors with null info
                        if(i >= curNode.nPrimitives){
                            leafNode.indices[i] = -1;
                            for(int j = 0; j < DIM; j++){
                                tripoints[0][j][i] = 0.;
                                tripoints[1][j][i] = 0.;
                                tripoints[2][j][i] = 0.;
                            }
                            continue;
                        }

                        leafNode.indices[i] = references[curNode.start + i].index;
                        const std::shared_ptr<Primitive<DIM>>& primitive = primitives[leafNode.indices[i]];
                        // 3d: fill triangle data
                        if(DIM == 3){
                            std::vector<Vector3f> vertices;
                            std::shared_ptr<Triangle> triangle = std::dynamic_pointer_cast<Triangle>(primitive);
                            triangle->getVertices(vertices);

                            for(int j = 0; j < DIM; j++){
                                tripoints[0][j][i] = vertices[0](j);
                                tripoints[1][j][i] = vertices[1](j);
                                tripoints[2][j][i] = vertices[2](j);
                            }
                        }
                        else{
                            LOG(FATAL) << "Non triangular primitives not handled at the moment";
                        }
                    }

                    // fill triangle vertices data to leaf node
                    leafNode.initPoints(tripoints);

                    // link leaf node to parent node
                    parentNode.indices[tempCounter] = buildLeaves.size() - 1;
                    parentNode.isLeaf[tempCounter] = true;
                    continue;
                }
                else{
                    // link interior node to parent node
                    parentNode.indices[tempCounter] = simdTreeIndex;
                    parentNode.isLeaf[tempCounter] = false;
                }
            }

            // push grandchildren [or whatever depth depending on vector width] of node on top of processing stack (leftmost grandchild goes to top of stack)
            nodeWorkingSet.emplace(BvhTraversalDepth(nodeIndex, 0));
            while(!nodeWorkingSet.empty()){
                // pop off node for processing
                BvhTraversalDepth traversal = nodeWorkingSet.top();
                nodeWorkingSet.pop();

                // parse node
                int ni = traversal.i;
                int d = traversal.depth;

                // process node
                if(d < maxDepth && nodes[ni].rightOffset != 0){
                    // node is not leaf and not grandchild, continue down tree
                    nodeWorkingSet.emplace(BvhTraversalDepth(ni + 1, d + 1));
                    nodeWorkingSet.emplace(BvhTraversalDepth(ni + nodes[ni].rightOffset, d + 1));
                }
                else{
                    // node is grandchild or leaf node (but not grandchild), push onto todo set
                    todo.emplace(BvhSimdBuildNode(ni, simdTreeIndex, toBuildDepth + 1));
                }
            }
        }

        // place tree and leaf nodes onto heap
        flatTree.clear();
        flatTree.reserve(nNodes);
        for(int n = 0; n < buildNodes.size(); n++){
            flatTree.emplace_back(buildNodes[n]);
        }
        leaves.clear();
        leaves.reserve(nLeaves);
        for(int n = 0; n < buildLeaves.size(); n++){
            leaves.emplace_back(buildLeaves[n]);
        }
    }

    template <int DIM, int W>
    inline BoundingBox<DIM> BvhSimd<DIM, W>::boundingBox() const{
        return bbox;
    }

    template <int DIM, int W>
    inline Vector<DIM> BvhSimd<DIM, W>::centroid() const{
        return bbox.centroid();
    }

    template <int DIM, int W>
    inline float BvhSimd<DIM, W>::surfaceArea() const{
        float area = 0.0;
        for(std::shared_ptr<Primitive<DIM>> p : primitives){
            area += p->surfaceArea();
        }
        return area;
    }

    template <int DIM, int W>
    inline float BvhSimd<DIM, W>::signedVolume() const{
        float volume = 0.0;
        for(std::shared_ptr<Primitive<DIM>> p : primitives){
            volume += p->signedVolume();
        }
        return volume;
    }

    template <int DIM, int W>
    inline int BvhSimd<DIM, W>::intersect(Ray<DIM>& r, std::vector<Interaction<DIM>>& is, bool checkOcclusion, bool countHits) const{

        return 0;
    }

/* ---- CPQ ---- */

    template <int DIM, int W>
    inline void processInteriorNode(std::queue<BvhTraversal>& queue, const BvhSimdFlatNode<DIM, W>& node, int& index, float& dMin){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
        queue.emplace(BvhTraversal(node.indices[index], dMin));
    }

    template <int DIM, int W>
    inline void processLeafNode(const BvhSimdFlatNode<DIM, W>& node, const std::vector<BvhSimdLeafNode<DIM, W>>& leaves, ParallelInteraction<DIM, W>& pi, SimdBoundingSphere<DIM, W>& sbs, BoundingSphere<DIM>& s, Interaction<DIM>& i, const std::vector<std::shared_ptr<Primitive<DIM>>>& primitives, const int& index){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif

        float bestDistance;
        float bestPoint[DIM];
        int bestIndex;

        // if mbvh leaf, process triangles in parallel
        const BvhSimdLeafNode<DIM, W>& leafNode = leaves[node.indices[index]];
        for(int k = 0; k < W; k++){
            pi.indices[k] = leafNode.indices[k];
        }

        // get closest point to all 4 children in parallel
        parallelTriangleClosestPoint<DIM, W>(leafNode, sbs, pi);

        // get info of closest point from parallel interaction
        pi.getBest(bestDistance, bestPoint, bestIndex);

        // if found best closest point beats out previous, update interaction
        if(bestIndex != -1 && bestDistance < s.r2){
            s.r2 = bestDistance;
            i.p = Vector<DIM>();
            for(int k = 0; k < DIM; k++){
                i.p(k) = (float)bestPoint[k];
            }
            i.n = Vector<DIM>(); // TEMPORARY!!!!
            i.d = std::sqrt(s.r2);
            i.primitive = primitives[bestIndex].get();
        }
    }

    inline void parseFlatNode(std::queue<BvhTraversal>& todo, int& ni, float& near){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
        // pop off the next node to work on
        BvhTraversal traversal = todo.front();
        todo.pop();

        // parse info about next node to work on
        ni = traversal.i;
        near = traversal.d;
    }

    template <int DIM, int W>
    inline void parseOverlap(const ParallelOverlapResult<W>& overlap, const int& index, float& dMax, float& dMin, BoundingSphere<DIM>& s){
        // #ifdef PROFILE
        //     PROFILE_SCOPED();
        // #endif
        // would be for better ordering
        // dMax = overlap.d2Max.vec[index];
        // dMin = overlap.d2Min.vec[index];
        dMax = overlap.d2Max.V.v[index];
        dMin = overlap.d2Min.V.v[index];

        // shorten if box is fully contained in query
        if(s.r2 > dMax){
            s.r2 = dMax;
        }
    }

    template <int DIM, int W>
    inline bool BvhSimd<DIM, W>::findClosestPoint(BoundingSphere<DIM>& s, Interaction<DIM>& i) const{
        #ifdef PROFILE
            PROFILE_SCOPED();
        #endif

        // useful variables
        std::queue<BvhTraversal> todo;
        SimdBoundingSphere<DIM, W> sbs(s);
        ParallelOverlapResult<W> overlap;
        ParallelInteraction<DIM, W> pi;

        int overallBest;
        float dMax, dMin;
        float prevRad = s.r2;

        // enqueue root node
        todo.emplace(BvhTraversal(0, minFloat));

        // work through tree
        while(!todo.empty()){
            int ni;
            float near;

            parseFlatNode(todo, ni, near);

            // // pop off the next node to work on
            // BvhTraversal traversal = todo.front();
            // todo.pop();

            // // parse info about next node to work on
            // int ni = traversal.i;
            // float near = traversal.d;
            const BvhSimdFlatNode<DIM, W>& node = flatTree[ni];

            // skip node if it is outside query radius
            if(near > s.r2){
                continue;
            }

            // do overlap test
            parallelOverlap<DIM, W>(node, sbs, overlap);

            // process overlapped nodes NOTE: ADD IN ORDERING ONCE THAT IS AVAILABLE
            for(int j = 0; node.indices[j] != -1 && j < W; j++){
                // // would be for better ordering
                // dMax = overlap.d2Max.vec[j];
                // dMin = overlap.d2Min.vec[j];

                // // shorten if box is fully contained in query
                // if(s.r2 > dMax){
                //     s.r2 = dMax;
                // }
                parseOverlap(overlap, j, dMax, dMin, s);

                // only process if box is in bounds of query
                if(s.r2 > dMin){
                    if(!node.isLeaf[j]){
                        // if interior node, enqueue traversal to this node
                        // todo.emplace(BvhTraversal(node.indices[j], dMin));
                        processInteriorNode(todo, node, j, dMin);
                    }
                    else{
                        processLeafNode(node, leaves, pi, sbs, s, i, primitives, j);
                        // // if mbvh leaf, process triangles in parallel
                        // const BvhSimdLeafNode<DIM, W>& leafNode = leaves[node.indices[j]];
                        // for(int k = 0; k < W; k++){
                        //     pi.indices[k] = leafNode.indices[k];
                        // }

                        // // get closest point to all 4 children in parallel
                        // parallelTriangleClosestPoint<DIM, W>(leafNode, sbs, pi);

                        // // get info of closest point from parallel interaction
                        // pi.getBest(bestDistance, bestPoint, bestIndex);

                        // // if found best closest point beats out previous, update interaction
                        // if(bestIndex != -1 && bestDistance < s.r2){
                        //     s.r2 = bestDistance;
                        //     i.p = Vector<DIM>();
                        //     for(int k = 0; k < DIM; k++){
                        //         i.p(k) = (float)bestPoint[k];
                        //     }
                        //     i.n = Vector<DIM>(); // TEMPORARY!!!!
                        //     i.d = std::sqrt(s.r2);
                        //     i.primitive = primitives[bestIndex].get();
                        // }
                    }
                }
            }
        }

        // return if we got a closest point
        return s.r2 != maxFloat;
    }
}// namespace fcpw