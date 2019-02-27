#ifndef GAME_ENGINE_BVH_H
#define GAME_ENGINE_BVH_H

#include "CoreLib/Basic.h"
#include "CoreLib/Graphics/BBox.h"
#include "Ray.h"

namespace GameEngine
{
    using namespace CoreLib::Basic;
    
    const int nBuckets = 16;

    class BvhNode
    {
    public:
        CoreLib::Graphics::BBox Bounds;
        unsigned int Axis : 2;
        unsigned int SkipBBoxTest : 1;
        int ElementCount : 29;
        union
        {
            int ElementId;
            int ChildOffset;
        };
        inline bool GetIsLeaf()
        {
            return ElementCount != 0;
        }
        inline int GetElementCount()
        {
            return ElementCount;
        }
    };

    template<typename T>
    class BvhNode_Build
    {
    public:
        CoreLib::Graphics::BBox Bounds;
        int Axis;
        T** Elements;
        int ElementCount;
        BvhNode_Build* Children[2];
        void AllocElements(int count)
        {
            Elements = (T**)malloc(count * sizeof(T*));
            ElementCount = count;
        }
        void FreeElements()
        {
            if (Elements)
            {
                free(Elements);
                Elements = 0;
            }
        }
        BvhNode_Build()
        {
            Children[0] = 0;
            Children[1] = 0;
            Axis = 0;
            ElementCount = 0;
            Elements = 0;
        }
        ~BvhNode_Build()
        {
            if (Children[0])
                delete Children[0];
            if (Children[1])
                delete Children[1];
            FreeElements();
        }
    };

    template<typename T>
    class Bvh_Build
    {
    public:
        CoreLib::RefPtr<BvhNode_Build<T>> Root;
        int ElementListSize;
        int NodeCount = 0;
    };

    template<typename T>
    class Bvh
    {
    private:
        int FlattenNodes(BvhNode_Build<T> * node)
        {
            int id = Nodes.Count();
            BvhNode n;
            n.Axis = node->Axis;
            n.Bounds = node->Bounds;
            if (node->Elements == 0)
                n.ElementCount = 0;
            else
                n.ElementCount = node->ElementCount;
            n.SkipBBoxTest = 0;
            Nodes.Add(n);

            if (node->Elements == 0)
            {
                FlattenNodes(node->Children[0]);
                Nodes[id].ChildOffset = FlattenNodes(node->Children[1]) - id;
            }
            else
            {
                Nodes[id].ElementId = Elements.Count();
                for (int i = 0; i < node->ElementCount; i++)
                    Elements.Add(*node->Elements[i]);
            }
            return id;
        }
    public:
        CoreLib::List<BvhNode> Nodes;
        CoreLib::List<T> Elements;
        void FromBuild(Bvh_Build<T> &bvh)
        {
            Nodes.Clear();
            Elements.Clear();
            Nodes.Reserve((int)bvh.NodeCount);
            Elements.Reserve((int)bvh.ElementListSize);
            FlattenNodes(bvh.Root.operator->());
        }
    };

    template<typename T>
    class BuildData
    {
    public:
        T * Element;
        CoreLib::Graphics::BBox Bounds;
        VectorMath::Vec3 Center;
    };

    inline float SurfaceArea(CoreLib::Graphics::BBox & box)
    {
        return ((box.xMax - box.xMin)*(box.yMax - box.yMin) + (box.xMax - box.xMin)*(box.zMax - box.zMax) + (box.yMax - box.yMin)*(box.zMax - box.zMin))*2.0f;
    }

    struct BucketInfo
    {
        int count;
        CoreLib::Graphics::BBox bounds;
        BucketInfo()
        {
            count = 0;
            bounds.Init();
        }
    };

    template<typename T, typename CostEvaluator>
    BvhNode_Build<T> * ConstructBvhNode(Bvh_Build<T> & tree, BuildData<T>* elements, int elementCount, int & elementListSize, int & nodeCount, CostEvaluator & eval, int depth)
    {
        BvhNode_Build<T> * node = new BvhNode_Build<T>();

        nodeCount = 1;
        elementListSize = 0;
        if (elementCount == 1 || depth == 61)
        {
            node->Bounds = elements->Bounds;
            node->AllocElements(1);
            node->Elements[0] = elements->Element;
            elementListSize = 1;
            return node;
        }
        else
        {
            CoreLib::Graphics::BBox centroidBounds;
            CoreLib::Graphics::BBox bbox;
            centroidBounds.Init();
            bbox.Init();
            for (int i = 0; i < elementCount; i++)
            {
                centroidBounds.Union(elements[i].Center);
                bbox.Union(elements[i].Bounds);
            }
            node->Bounds = bbox;
            int dim = centroidBounds.MaxDimension();

            if (centroidBounds.Min[dim] == centroidBounds.Max[dim])
            {
                node->Bounds = bbox;
                node->AllocElements((int)elementCount);
                for (int i = 0; i < (int)elementCount; i++)
                {
                    node->Elements[i] = elements[i].Element;
                }
                elementListSize = elementCount;
                return node;
            }

            BucketInfo buckets[nBuckets];
            if (elementCount > (2 << 12))
            {
                const int processorCount = 16;
                BucketInfo buckets_proc[processorCount][nBuckets];
                int blockSize = (int)(elementCount / processorCount);
                #pragma omp parallel for
                for (int procId = 0; procId < processorCount; procId++)
                {
                    int end;
                    if (procId == processorCount - 1)
                        end = (int)elementCount;
                    else
                        end = (procId + 1)*blockSize;
                    for (int i = procId * blockSize; i < end; i++)
                    {
                        int b = (int)(nBuckets *
                            ((elements[i].Center[dim] - centroidBounds.Min[dim]) / (centroidBounds.Max[dim] - centroidBounds.Min[dim])));
                        if (b == nBuckets) b = nBuckets - 1;
                        buckets_proc[procId][b].count++;
                        buckets_proc[procId][b].bounds.Union(elements[i].Bounds);
                    }
                }
                for (int i = 0; i < nBuckets; i++)
                {
                    for (int j = 0; j < processorCount; j++)
                    {
                        buckets[i].count += buckets_proc[j][i].count;
                        buckets[i].bounds.Union(buckets_proc[j][i].bounds);
                    }
                }
            }
            else
            {
                for (int i = 0; i < elementCount; i++)
                {
                    int b = (int)(nBuckets *
                        ((elements[i].Center[dim] - centroidBounds.Min[dim]) / (centroidBounds.Max[dim] - centroidBounds.Min[dim])));
                    if (b == nBuckets) b = nBuckets - 1;
                    buckets[b].count++;
                    buckets[b].bounds.Union(elements[i].Bounds);
                }
            }

            CoreLib::Graphics::BBox bounds1[nBuckets - 1];
            bounds1[nBuckets - 2] = buckets[nBuckets - 1].bounds;
            for (int i = nBuckets - 3; i >= 0; i--)
            {
                bounds1[i].Init();
                bounds1[i].Union(buckets[i + 1].bounds);
                bounds1[i].Union(bounds1[i + 1]);
            }
            CoreLib::Graphics::BBox b0;
            b0.Init();
            int count0 = 0;
            float minCost = FLT_MAX;
            int minCostSplit = 0;
            for (int i = 0; i < nBuckets - 1; i++)
            {
                b0.Union(buckets[i].bounds);
                count0 += buckets[i].count;
                int count1 = (int)elementCount - count0;
                float cost = eval.EvalCost(count0, SurfaceArea(b0), count1, SurfaceArea(bounds1[i]), SurfaceArea(bbox));
                if (cost < minCost)
                {
                    minCost = cost;
                    minCostSplit = i;
                }
            }

            if (elementCount > CostEvaluator::ElementsPerNode ||
                minCost < elementCount)
            {
                BuildData<T> *pmid = std::partition(elements,
                    elements + elementCount,
                    [&](const BuildData<T> &p)
                {
                    int b = (int)(nBuckets * ((p.Center[dim] - centroidBounds.Min[dim]) /
                        (centroidBounds.Max[dim] - centroidBounds.Min[dim])));
                    if (b == nBuckets) b = nBuckets - 1;
                    return b <= minCostSplit;
                });
                node->Axis = dim;
                int listSize1, listSize2;
                int nodeCount1, nodeCount2;
                if (depth > 8)
                {
                    node->Children[0] = ConstructBvhNodeNonRec<T, CostEvaluator>(elements, (int)(pmid - elements), listSize1, nodeCount1, eval);
                    node->Children[1] = ConstructBvhNodeNonRec<T, CostEvaluator>(pmid, (int)(elements + elementCount - pmid), listSize2, nodeCount2, eval);
                }
                else
                {
                    #pragma omp parallel sections
                    {
                        #pragma omp section
                        node->Children[0] = ConstructBvhNode<T, CostEvaluator>(tree, elements, (int)(pmid - elements), listSize1, nodeCount1, eval, depth + 1);

                        #pragma omp section
                        node->Children[1] = ConstructBvhNode<T, CostEvaluator>(tree, pmid, (int)(elements + elementCount - pmid), listSize2, nodeCount2, eval, depth + 1);
                    }
                }
                node->ElementCount = (int)(elementListSize = listSize1 + listSize2);
                nodeCount += nodeCount1 + nodeCount2;
            }
            else
            {
                node->AllocElements((int)elementCount);
                node->Bounds = bbox;
                for (int i = 0; i < (int)elementCount; i++)
                {
                    node->Elements[i] = elements[i].Element;
                }
                elementListSize = elementCount;
            }
            return node;
        }
    }

    template<typename T, typename CostEvaluator>
    BvhNode_Build<T> * ConstructBvhNodeNonRec(BuildData<T>* elements, int elementCount, int & elementListSize, int & nodeCount, CostEvaluator & eval)
    {
        struct BvhJob
        {
            BvhNode_Build<T> ** result;
            BuildData<T>* elements;
            int elementCount;
            BvhJob()
            {}
            BvhJob(BvhNode_Build<T> ** result, BuildData<T>* elements, int elementCount)
            {
                this->elements = elements;
                this->elementCount = elementCount;
                this->result = result;
            }
        };
        const int stackSize = 256;
        BvhJob stack[stackSize];
        int stackPtr = 0;
        auto pushJob = [&](BvhNode_Build<T> ** result, BuildData<T>* elements, int elementCount)
        {
            BvhJob job(result, elements, elementCount);
            if (stackPtr < stackSize)
                stack[stackPtr++] = job;
            else
                throw "stack overflow";
        };
        auto popJob = [&]()->BvhJob
        {
            if (stackPtr)
                return stack[--stackPtr];
            else
                throw "stack empty";
        };

        BvhNode_Build<T> * rs = 0;
        nodeCount = 0;
        elementListSize = 0;
        BvhJob job(&rs, elements, elementCount);
        while (true)
        {
            BvhNode_Build<T> * node = new BvhNode_Build<T>();
            nodeCount++;
            (*job.result) = node;
            BuildData<T>* jElements = job.elements;
            int jElementCount = job.elementCount;
            if (jElementCount == 0)
            {
                printf("elementCount = 0 !");
                throw 0;
            }
            if (jElementCount == 1 || stackPtr == stackSize)
            {
                node->Bounds = jElements->Bounds;
                node->AllocElements((int)jElementCount);
                for (int i = 0; i < (int)jElementCount; i++)
                {
                    node->Elements[i] = jElements[i].Element;
                }
                elementListSize += jElementCount;
                if (!stackPtr)
                    break;
                else
                    job = popJob();
                continue;
            }
            else
            {
                CoreLib::Graphics::BBox centroidBounds;
                CoreLib::Graphics::BBox bbox;
                centroidBounds.Init();
                bbox.Init();
                for (int i = 0; i < jElementCount; i++)
                {
                    centroidBounds.Union(jElements[i].Center);
                    bbox.Union(jElements[i].Bounds);
                }
                node->Bounds = bbox;
                int dim = centroidBounds.MaxDimension();

                if (centroidBounds.Min[dim] == centroidBounds.Max[dim])
                {
                    node->Bounds = bbox;
                    node->AllocElements((int)jElementCount);
                    for (int i = 0; i < (int)jElementCount; i++)
                    {
                        node->Elements[i] = jElements[i].Element;
                    }
                    elementListSize += jElementCount;
                    if (!stackPtr)
                        break;
                    else
                        job = popJob();
                    continue;
                }

                BucketInfo buckets[nBuckets];
                for (int i = 0; i < jElementCount; i++)
                {
                    int b = (int)(nBuckets *
                        ((jElements[i].Center[dim] - centroidBounds.Min[dim]) / (centroidBounds.Max[dim] - centroidBounds.Min[dim])));
                    if (b == nBuckets) b = nBuckets - 1;
                    buckets[b].count++;
                    buckets[b].bounds.Union(jElements[i].Bounds);
                }

                float minCost = FLT_MAX;
                int minCostSplit = 0;
                CoreLib::Graphics::BBox bounds1[nBuckets - 1];
                bounds1[nBuckets - 2] = buckets[nBuckets - 1].bounds;
                for (int i = nBuckets - 3; i >= 0; i--)
                {
                    bounds1[i].Init();
                    bounds1[i].Union(buckets[i + 1].bounds);
                    bounds1[i].Union(bounds1[i + 1]);
                }
                CoreLib::Graphics::BBox b0;
                b0.Init();
                int count0 = 0;
                for (int i = 0; i < nBuckets - 1; i++)
                {
                    b0.Union(buckets[i].bounds);
                    count0 += buckets[i].count;
                    int count1 = (int)jElementCount - count0;
                    float cost = eval.EvalCost(count0, SurfaceArea(b0), count1, SurfaceArea(bounds1[i]), SurfaceArea(bbox));
                    if (cost < minCost)
                    {
                        minCost = cost;
                        minCostSplit = i;
                    }
                }
                if (jElementCount > CostEvaluator::ElementsPerNode ||
                    minCost < jElementCount)
                {
                    BuildData<T> *pmid = std::partition(jElements,
                        jElements + jElementCount,
                        [&](const BuildData<T> &p)
                    {
                        int b = (int)(nBuckets * ((p.Center[dim] - centroidBounds.Min[dim]) /
                            (centroidBounds.Max[dim] - centroidBounds.Min[dim])));
                        if (b == nBuckets) b = nBuckets - 1;
                        return b <= minCostSplit;
                    });
                    node->Axis = dim;
                    job = BvhJob(node->Children, jElements, (int)(pmid - jElements));
                    pushJob(node->Children + 1, pmid, (int)(jElements + jElementCount - pmid));
                }
                else
                {
                    node->AllocElements((int)jElementCount);
                    node->Bounds = bbox;
                    for (int i = 0; i < (int)jElementCount; i++)
                    {
                        node->Elements[i] = jElements[i].Element;
                    }
                    elementListSize += jElementCount;
                    if (!stackPtr)
                        break;
                    else
                        job = popJob();
                    continue;
                }
            }
        }
        return rs;
    }

    template<typename T, typename CostEvaluator>
    void ConstructBvh(Bvh_Build<T> & tree, BuildData<T>* elements, int elementCount, CostEvaluator & eval)
    {
        tree.Root = ConstructBvhNode<T, CostEvaluator>(tree, elements, elementCount, tree.ElementListSize, tree.NodeCount, eval, 0);
    }

    template<typename T, typename Tracer, typename THit, bool pred>
    bool TraverseBvh(const Tracer & tracer, THit& rs, Bvh<T> & tree, const Ray & ray, VectorMath::Vec3 rcpDir)
    {
        bool hit = false;
        float tmax = ray.tMax;
        int dirIsNeg[3] = { rcpDir.x < 0, rcpDir.y < 0, rcpDir.z < 0 };
        BvhNode* node = tree.Nodes.Buffer();
        int todoOffset = 0;
        BvhNode* todo[256];
        auto traceRay = ray;
        while (true)
        {
            float t1, t2;
            if (RayBBoxIntersection(node->Bounds, ray.Origin, rcpDir, t1, t2) && t1 < traceRay.tMax)
            {
                if (node->ElementCount > 0)
                {
                    THit inter;
                    for (int i = node->ElementId; i < node->ElementId + node->ElementCount; i++)
                    {
                        if (tracer.Trace(inter, tree.Elements[i], traceRay, tmax))
                        {
                            if (pred)
                                return true;
                            if (tmax <= traceRay.tMax)
                            {
                                rs = inter;
                                traceRay.tMax = tmax;
                                hit = true;
                            }
                        }
                    }
                    if (todoOffset == 0) break;
                    node = todo[--todoOffset];
                }
                else
                {
                    if (ray.Origin[node->Axis] > (node + 1)->Bounds.Max[node->Axis])
                    {
                        todo[todoOffset++] = node + 1;
                        node = node + node->ChildOffset;
                    }
                    else
                    {
                        todo[todoOffset++] = node + node->ChildOffset;
                        node = node + 1;
                    }
                }
            }
            else
            {
                if (todoOffset == 0)
                    break;
                node = todo[--todoOffset];
            }
        }
        return hit;
    }
}

#endif