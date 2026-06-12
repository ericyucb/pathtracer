#include<simd/simd.h>
#include "scene.h"
#include <queue>
using namespace simd;
using namespace std;

//we would have an array containing all nodes
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


            while (bfsQueue.size() != 0) {
                int parentIndex = bfsQueue.front();
                BVHNode& parent = bvhNodes[parentIndex];
                bfsQueue.pop();

                if (parent.primitiveCount <= 5) {
                    continue;
                }
                float3 minP = parent.minP;
                float3 maxP = parent.maxP;
                float3 diff = maxP - minP;

                //this finds the axis to split on
                int axis = 0;
                if (diff[1] > diff[axis]) {
                    axis = 1;
                }
                if (diff[2] > diff[axis]){
                    axis = 2;
                }

            
                float3 splitPosition = minP + diff[axis] * 0.5f;
                int leftPointer = parent.firstPrimitiveIndex;
                for (int i = parent.firstPrimitiveIndex; i < parent.firstPrimitiveIndex + parent.primitiveCount; i++) {
                    if (primitives[i].centroid[axis] < splitPosition[axis] and i == leftPointer) {
                        leftPointer += 1;
                        continue;
                    }
                    if (primitives[i].centroid[axis] < splitPosition[axis] and i != leftPointer) {
                        BVHPrimitive temp = primitives[leftPointer];
                        primitives[leftPointer] = primitives[i];
                        primitives[i] = temp;
                        leftPointer += 1;
                    }
                }

                BVHNode leftChild;
                leftChild.firstPrimitiveIndex = parent.firstPrimitiveIndex;
                leftChild.primitiveCount = leftPointer - parent.firstPrimitiveIndex;
                if (leftChild.primitiveCount == 0 || leftChild.primitiveCount == parent.primitiveCount){ // this is to prevent infinite recursion cases
                    continue;
                }
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
                // parent.rightChildIndex = rightChildIndex;

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
