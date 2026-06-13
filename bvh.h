#include<simd/simd.h>
#include "scene.h"
#include <queue>
using namespace simd;
using namespace std;

struct BVHNode { // each node should encode its bounding box as well as all primitives contained within it
    float3 minP;
    float3 maxP;
    int firstPrimitiveIndex;
    int primitiveCount;
    int leftChildIndex; // if leftChildIndex = 0 it is a leaf
    // int rightChildIndex;
};

struct BVHPrimitive {
    float3 centroid;
    float3 minP;
    float3 maxP;
    int objectIndex;
    int objectType;
};

//this is used by the gpu only
struct PrimitiveRef{
    int objectType;
    int objectIndex;
};

struct BVHData{
    vector<BVHNode> bvhNodes;
    vector<PrimitiveRef> primitiveRef;
};

class BVH{
    private:
        vector<BVHPrimitive> buildPrimitive(Sphere* spheres, Triangle* triangles, int sphereCount, int triangleCount) {
            vector<BVHPrimitive> primitives(sphereCount + triangleCount);
            for (int i = 0; i < sphereCount; i++) {
                BVHPrimitive spherePrimitive;
                Sphere s1 = spheres[i];
                float3 centroid = float3{s1.positionAndRadius[0], s1.positionAndRadius[1], s1.positionAndRadius[2]};
                spherePrimitive.centroid = centroid;
                spherePrimitive.objectIndex = i;
                spherePrimitive.objectType = 1;
                spherePrimitive.minP = centroid - float3{s1.positionAndRadius[3], s1.positionAndRadius[3], s1.positionAndRadius[3]};
                spherePrimitive.maxP = centroid + float3{s1.positionAndRadius[3], s1.positionAndRadius[3], s1.positionAndRadius[3]};
                primitives[i] = spherePrimitive;
            }
            for (int i = 0; i < triangleCount; i++) {
                BVHPrimitive trianglePrimitive;
                Triangle t = triangles[i];
                float3 centroid = (t.p1 + t.p2 + t.p3) / 3.0f;
                trianglePrimitive.centroid = centroid;
                trianglePrimitive.objectIndex = i;
                trianglePrimitive.objectType = 2;
                trianglePrimitive.minP = simd::min(simd::min(t.p1, t.p2), t.p3);
                trianglePrimitive.maxP = simd::max(simd::max(t.p1, t.p2), t.p3);
                primitives[sphereCount + i] = trianglePrimitive;
            }

            return primitives;
        }

        void updateNodeBounds(int BVHNodeIndex, BVHPrimitive* primitives, BVHNode* bvhNodes) {

            BVHNode& bvhNode = bvhNodes[BVHNodeIndex];
            float3 minP = float3(1e30f);
            float3 maxP = float3(-1e30f);
            for (int i = bvhNode.firstPrimitiveIndex; i < bvhNode.firstPrimitiveIndex + bvhNode.primitiveCount; i++) {
                minP = simd::min(minP, primitives[i].minP);
                maxP = simd::max(maxP, primitives[i].maxP);
            };
            bvhNode.minP = minP;
            bvhNode.maxP = maxP;
        }

        // float calculateSAHCost(BVHNode parent, float splitProportion, float3 minP, float3 maxP, float3 diff, int axis, int leftCount){
        //     float3 leftMax = maxP;
        //     leftMax[axis] = minP[axis] + diff[axis] * splitProportion;
        //     float3 leftLengths = leftMax - minP;
        //     float surfaceAreaLeft = 2*(leftLengths[0] * leftLengths[1]) + 2*(leftLengths[1] * leftLengths[2]) + 2*(leftLengths[0] * leftLengths[2]);
        //     //find the area of the right rectangular prism
        //     float3 rightMin = minP;
        //     rightMin[axis] += diff[axis] * splitProportion;
        //     float3 rightLengths = maxP - rightMin;
        //     float surfaceAreaRight = 2*(rightLengths[0]* rightLengths[1]) + 2*(rightLengths[1]*rightLengths[2]) + 2*(rightLengths[0]*rightLengths[2]);
        //     //find the # of elements in the right rectangular prism
        //     int rightCount = parent.primitiveCount - leftCount;

        //     float SAHCost = surfaceAreaLeft*(float)leftCount + surfaceAreaRight*(float)rightCount;
        //     return SAHCost;
        // }

        pair<float, int> evaluateSplit(BVHPrimitive* primitives, BVHNode& parent, float splitPoint, int axis){ //returns left Count
            int leftCount = 0;
            int rightCount = 0;
            float3 leftMinP = float3(1e30f);
            float3 leftMaxP = float3(-1e30f);
            float3 rightMinP = float3(1e30f);
            float3 rightMaxP = float3(-1e30f);

            for (int i = parent.firstPrimitiveIndex; i<parent.firstPrimitiveIndex + parent.primitiveCount; i++) {
            
                if (primitives[i].centroid[axis] < splitPoint) {
                    leftCount += 1;
                    leftMinP = simd::min(leftMinP, primitives[i].minP);
                    leftMaxP = simd::max(leftMaxP, primitives[i].maxP);
                    
                } else {
                    rightCount += 1;
                    rightMinP = simd::min(rightMinP, primitives[i].minP);
                    rightMaxP = simd::max(rightMaxP, primitives[i].maxP);
                }
            
            }

            float surfaceAreaLeft = 0.0f;
            float surfaceAreaRight = 0.0f;

            // Fix #4: Protect against empty splits causing Inf/NaN math
            if (leftCount > 0) {
                float3 leftLengths = leftMaxP - leftMinP;
                surfaceAreaLeft = 2*(leftLengths[0] * leftLengths[1]) + 2*(leftLengths[1] * leftLengths[2]) + 2*(leftLengths[0] * leftLengths[2]);
            }
            
            if (rightCount > 0) {
                float3 rightLengths = rightMaxP - rightMinP;
                surfaceAreaRight = 2*(rightLengths[0]* rightLengths[1]) + 2*(rightLengths[1]*rightLengths[2]) + 2*(rightLengths[0]*rightLengths[2]);
            }

            return {leftCount * surfaceAreaLeft + rightCount * surfaceAreaRight, leftCount};
         
        }

        void split(vector<BVHPrimitive> &primitives, BVHNode& parent, float splitPoint, int axis){
            int leftIndex = parent.firstPrimitiveIndex;
            for (int i = parent.firstPrimitiveIndex; i < parent.firstPrimitiveIndex + parent.primitiveCount; i++) {
                if (primitives[i].centroid[axis] < splitPoint and i == leftIndex) {
                    leftIndex += 1;
                    continue;
                } else if(primitives[i].centroid[axis] < splitPoint and i != leftIndex) {
                    BVHPrimitive temp = primitives[leftIndex];
                    primitives[leftIndex] = primitives[i];
                    primitives[i] = temp;
                    leftIndex += 1;
                }
            }
        }

      

    public:
        BVHData buildBVH(Sphere* spheres, Triangle* triangles, int sphereCount, int triangleCount){
            vector<BVHPrimitive> primitives = buildPrimitive(spheres, triangles, sphereCount, triangleCount); // this gets you a vector of all BVHPrimitive primitives
            int primitiveCount = sphereCount + triangleCount;
            vector<BVHNode> bvhNodes(2*primitiveCount); //this allocates space for all nodes in the BVH 

            BVHNode& root = bvhNodes[0];
            int nodesUsed = 1;
            root.leftChildIndex = 0;
            root.firstPrimitiveIndex = 0;
            root.primitiveCount = primitiveCount;
            updateNodeBounds(0, primitives.data(), bvhNodes.data());

            std::queue<int> bfsQueue;
            bfsQueue.push(0);

            int size = 1;
            int depth = 1;
            while (bfsQueue.size() != 0 and depth <= 20) {
                int parentIndex = bfsQueue.front();
                BVHNode& parent = bvhNodes[parentIndex];
                bfsQueue.pop();

                size -= 1;
                if (size == 0) {
                    depth += 1;
                    size = bfsQueue.size();
                }

                float3 minP = parent.minP;
                float3 maxP = parent.maxP;
                float3 diff = maxP - minP;

                //for each axis
                //evaluate 10 splits
                //choose best split using sah
                int maxSplits = 10;
                int bestAxis = 0;
                int bestLeftCount = 0;
                float bestSplitPoint = minP[0] + diff[0] * 0.1f;
                float bestSAHCost = std::numeric_limits<float>::infinity();

                for (int axis=0; axis<3; axis++){
                    for (int split=1; split  <= maxSplits; split++){
                        //evaluates one split
                        float splitProportion = (float)split/maxSplits;
                        float splitPoint = minP[axis] + diff[axis] * splitProportion;
                        //find the # of elements in the left rectangular prism
                        auto [SAHCost, leftCount] = evaluateSplit(primitives.data(), parent, splitPoint, axis);
                        //find the area of the left rectangular prism
                        // float SAHCost = calculateSAHCost(parent, splitProportion, minP, maxP, diff, axis, leftCount);

                        if (SAHCost < bestSAHCost) {
                            bestSAHCost = SAHCost;
                            bestAxis = axis;
                            bestSplitPoint = splitPoint;
                            bestLeftCount = leftCount;
                        }
                    }
                }
                //sah cost of parent
                float parentSAHCost = 2*(diff[0] * diff[1] + diff[1]* diff[2] + diff[0]*diff[2]) * parent.primitiveCount;
                if (bestSAHCost > parentSAHCost || bestLeftCount == 0 || bestLeftCount == parent.primitiveCount) {
                    continue; // degenerate split, leave as a leaf
                }

                split(primitives, parent, bestSplitPoint, bestAxis);


                BVHNode leftChild;
                leftChild.firstPrimitiveIndex = parent.firstPrimitiveIndex;
                leftChild.primitiveCount = bestLeftCount;
                leftChild.leftChildIndex = 0;
                int leftChildIndex = nodesUsed++;
                bvhNodes[leftChildIndex] = leftChild;
                updateNodeBounds(leftChildIndex, primitives.data(), bvhNodes.data());
                bfsQueue.push(leftChildIndex);

                BVHNode rightChild;
                rightChild.firstPrimitiveIndex = parent.firstPrimitiveIndex + leftChild.primitiveCount;
                rightChild.primitiveCount = parent.primitiveCount - leftChild.primitiveCount;
                rightChild.leftChildIndex = 0;
                int rightChildIndex = nodesUsed++;
                bvhNodes[rightChildIndex] = rightChild;
                updateNodeBounds(rightChildIndex, primitives.data(), bvhNodes.data());
                bfsQueue.push(rightChildIndex);

                parent.leftChildIndex = leftChildIndex;

            }

            vector<PrimitiveRef> primitivesRef(sphereCount + triangleCount);
            for (int i = 0; i < sphereCount + triangleCount; i++) {
                PrimitiveRef curr;
                curr.objectIndex = primitives[i].objectIndex;
                curr.objectType = primitives[i].objectType;
                primitivesRef[i] = curr;
            }


            BVHData bvhData;
            bvhData.bvhNodes = bvhNodes;
            bvhData.primitiveRef = primitivesRef;
            return bvhData;
        };

     

};
