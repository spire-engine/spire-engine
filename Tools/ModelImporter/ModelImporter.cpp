//#include "FBXImport/include/Importer.hpp"
//#include "FBXImport/include/scene.h"
//#include "FBXImport/include/postprocess.h"
#include "CoreLib/Basic.h"
#include "CoreLib/LibIO.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "LightmapUVGeneration.h"
#include "WinForm/WinApp.h"
#include "WinForm/WinButtons.h"
#include "WinForm/WinCommonDlg.h"
#include "WinForm/WinForm.h"
#include "WinForm/WinTextBox.h"
#include <fbxsdk.h>

using namespace CoreLib;
using namespace CoreLib::IO;
using namespace GameEngine;
using namespace VectorMath;
using namespace fbxsdk;

Quaternion ToQuaternion(const FbxVector4 &q)
{
    Quaternion rs;
    rs.x = (float)q[0];
    rs.y = (float)q[1];
    rs.z = (float)q[2];
    rs.w = (float)q[3];
    return rs;
}

void IndentString(StringBuilder &sb, String src)
{
    int indent = 0;
    bool beginTrim = true;
    for (int c = 0; c < src.Length(); c++)
    {
        auto ch = src[c];
        if (ch == '\n')
        {
            sb << "\n";

            beginTrim = true;
        }
        else
        {
            if (beginTrim)
            {
                while (c < src.Length() - 1 && (src[c] == '\t' || src[c] == '\n' || src[c] == '\r' || src[c] == ' '))
                {
                    c++;
                    ch = src[c];
                }
                for (int i = 0; i < indent - 1; i++)
                    sb << '\t';
                if (ch != '}' && indent > 0)
                    sb << '\t';
                beginTrim = false;
            }

            if (ch == '{')
                indent++;
            else if (ch == '}')
                indent--;
            if (indent < 0)
                indent = 0;

            sb << ch;
        }
    }
}

class ExportArguments
{
public:
    String FileName;
    String RootNodeName;
    String FileNameSuffix;
    String MeshPathPrefix;
    String IgnorePattern;
    Quaternion RootTransform = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    Quaternion RootFixTransform = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    bool ForceRecomputeNormal = false;
    bool NoBlendShapeNormals = false;
    bool ExportSkeleton = true;
    bool ExportMesh = true;
    bool ExportAnimation = true;
    bool FlipUV = false;
    bool FlipWindingOrder = false;
    bool FlipYZ = false;
    bool CreateMeshFromSkeleton = false;
    bool RemoveNamespace = false;
    bool ExportScene = false;
};

using namespace CoreLib::WinForm;

Vec3 FlipYZ(const Vec3 &v)
{
    return Vec3::Create(v.x, v.z, -v.y);
}

Matrix4 FlipYZ(const Matrix4 &v)
{
    Matrix4 rs = v;
    for (int i = 0; i < 4; i++)
    {
        rs.m[1][i] = v.m[2][i];
        rs.m[2][i] = -v.m[1][i];
    }
    for (int i = 0; i < 4; i++)
    {
        Swap(rs.m[i][1], rs.m[i][2]);
        rs.m[i][2] = -rs.m[i][2];
    }
    return rs;
}

void FlipKeyFrame(BoneTransformation &kf)
{
    Matrix4 transform = kf.ToMatrix();
    Matrix4 rotX90, rotXNeg90;
    Matrix4::RotationX(rotX90, Math::Pi * 0.5f);
    Matrix4::RotationX(rotXNeg90, -Math::Pi * 0.5f);

    Matrix4::Multiply(transform, transform, rotX90);
    Matrix4::Multiply(transform, rotXNeg90, transform);

    kf.Rotation = Quaternion::FromMatrix(transform.GetMatrix3());
    kf.Rotation *= 1.0f / kf.Rotation.Length();
    kf.Translation = Vec3::Create(transform.values[12], transform.values[13], transform.values[14]);

    Swap(kf.Scale.y, kf.Scale.z);
}

void GetSkeletonNodes(List<FbxNode *> &nodes, FbxNode *node)
{
    nodes.Add(node);
    for (auto i = 0; i < node->GetChildCount(); i++)
        GetSkeletonNodes(nodes, node->GetChild(i));
}

class RootTransformApplier
{
public:
    Quaternion rootTransform, invRootTransform;
    Matrix4 invRootTransformM;
    Matrix4 rootTransformM;

    RootTransformApplier(Quaternion transform)
    {
        rootTransform = transform;
        invRootTransform = rootTransform.Inverse();
        invRootTransformM = invRootTransform.ToMatrix4();
        rootTransformM = rootTransform.ToMatrix4();
    }
    static BoneTransformation ApplyFix(const Matrix4 &transform, const BoneTransformation &bt)
    {
        BoneTransformation rs;
        Matrix4 m;
        Matrix4::Multiply(m, transform, bt.ToMatrix());
        rs.FromMatrix(m);

        float X0, Y0, Z0, X1, Y1, Z1;
        QuaternionToEulerAngle(bt.Rotation, X0, Y0, Z0, EulerAngleOrder::YZX);
        QuaternionToEulerAngle(rs.Rotation, X1, Y1, Z1, EulerAngleOrder::YZX);
        return rs;
    }
    BoneTransformation Apply(const BoneTransformation &bt)
    {
        BoneTransformation rs;
        Matrix4 T;
        Matrix4::Translation(T, bt.Translation.x, bt.Translation.y, bt.Translation.z);
        Matrix4 m1;
        Matrix4::Multiply(m1, T, invRootTransformM);
        Matrix4::Multiply(m1, rootTransformM, m1);
        rs.Translation = Vec3::Create(m1.values[12], m1.values[13], m1.values[14]);
        rs.Rotation = rootTransform * bt.Rotation * invRootTransform;
        Matrix4 S;
        Matrix4::Scale(S, bt.Scale.x, bt.Scale.y, bt.Scale.z);
        Matrix4::Multiply(m1, S, invRootTransformM);
        Matrix4::Multiply(m1, rootTransformM, m1);
        rs.Scale = Vec3::Create(m1.values[0], m1.values[5], m1.values[10]);
        float X0, Y0, Z0, X1, Y1, Z1;
        QuaternionToEulerAngle(bt.Rotation, X0, Y0, Z0, EulerAngleOrder::YZX);
        QuaternionToEulerAngle(rs.Rotation, X1, Y1, Z1, EulerAngleOrder::YZX);
        Vec3 norm = rs.Rotation.Transform(Vec3::Create(1.0f, 0.f, 0.f));

        return rs;
    }
};

void PrintNode(FbxNode *pNode, int numTabs = 0)
{
    for (int i = 0; i < numTabs; i++)
        printf("  ");
    const char *nodeName = pNode->GetName();
    FbxDouble3 translation = pNode->LclTranslation.Get();
    FbxDouble3 rotation = pNode->LclRotation.Get();
    FbxDouble3 scaling = pNode->LclScaling.Get();

    // Print the contents of the node.
    printf("<node type='%s' name='%s' translation='(%f, %f, %f)' rotation='(%f, %f, %f)' scaling='(%f, %f, %f)'>\n",
        pNode->GetTypeName(), nodeName, translation[0], translation[1], translation[2], rotation[0], rotation[1],
        rotation[2], scaling[0], scaling[1], scaling[2]);

    // Recursively print the children.
    for (int j = 0; j < pNode->GetChildCount(); j++)
        PrintNode(pNode->GetChild(j), numTabs + 1);
    for (int i = 0; i < numTabs; i++)
        printf("  ");
    printf("</node>\n");
}

Matrix4 GetMatrix(FbxMatrix m)
{
    Matrix4 rs;
    for (int i = 0; i < 16; i++)
        rs.values[i] = (float)m.Get(i >> 2, i & 3);
    return rs;
}

Vec2 GetVec2(FbxVector2 v)
{
    Vec2 rs;
    rs.x = (float)v[0];
    rs.y = (float)v[1];
    return rs;
}

Vec4 GetVec4(FbxColor v)
{
    Vec4 rs;
    rs.x = (float)v.mRed;
    rs.y = (float)v.mGreen;
    rs.z = (float)v.mBlue;
    rs.w = (float)v.mAlpha;
    return rs;
}

Vec3 GetVec3(FbxVector4 v)
{
    Vec3 rs;
    rs.x = (float)v[0];
    rs.y = (float)v[1];
    rs.z = (float)v[2];
    return rs;
}

FbxNode *FindFirstSkeletonNode(FbxNode *node)
{
    if (node->GetSkeleton())
        return node;
    for (int i = 0; i < node->GetChildCount(); i++)
    {
        auto rs = FindFirstSkeletonNode(node->GetChild(i));
        if (rs)
            return rs;
    }
    return nullptr;
}

struct SceneStaticObject
{
    String name;
    FbxMesh *mesh;
    FbxNode *node;
    Matrix4 transform;
};

String RemoveNamespace(String origin, bool removeNamespace)
{
    if (!removeNamespace)
        return origin;

    auto rootNames = Split(origin, ':');
    if (rootNames.Count() > 1)
    {
        return rootNames.Last();
    }
    return origin;
}

Mesh ExportMesh(
    List<SceneStaticObject> &objs, ExportArguments &args, List<fbxsdk::FbxNode *> &skeletonNodes, List<int> &matIndices)
{
    int numColorChannels = 0;
    int numUVChannels = 0;
    int numVerts = 0;
    int numFaces = 0;
    bool hasBones = false;
    for (auto obj : objs)
    {
        auto mesh = obj.mesh;
        numColorChannels = Math::Max(numColorChannels, (int)mesh->GetElementVertexColorCount());
        numUVChannels = Math::Max(numUVChannels, (int)mesh->GetElementUVCount());
        int deformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
        hasBones |= (deformerCount != 0);
        for (auto i = 0; i < mesh->GetPolygonCount(); i++)
        {
            numVerts += mesh->GetPolygonSize(i);
            if (mesh->GetPolygonSize(i) >= 3)
                numFaces += mesh->GetPolygonSize(i) - 2;
        }
    }

    RefPtr<Mesh> meshOut = new Mesh();
    meshOut->Bounds.Init();
    meshOut->SetVertexFormat(MeshVertexFormat(numColorChannels, numUVChannels, true, hasBones));
    meshOut->AllocVertexBuffer(numVerts);
    int vertPtr = 0;
    int blendShapeChannelCount = 0;

    for (auto &obj : objs)
    {
        EnumerableDictionary<int, List<int>> indexBuffers;

        auto transformMat = obj.transform;
        auto mesh = obj.mesh;
        Matrix4::Multiply(transformMat, args.RootTransform.ToMatrix4(), transformMat);
        Matrix4 normMat;
        transformMat.GetNormalMatrix(normMat);
        wprintf(L"Mesh element %d: %s\n", meshOut->ElementRanges.Count(), obj.name.ToWString());

        int meshNumFaces = 0;
        int meshNumVerts = 0;
        int faceId = 0;

        for (auto i = 0; i < mesh->GetPolygonCount(); i++)
        {
            if (mesh->GetPolygonSize(i) >= 3)
                meshNumFaces += mesh->GetPolygonSize(i) - 2;
            meshNumVerts += mesh->GetPolygonSize(i);
        }
        int startVertId = vertPtr;
        vertPtr += meshNumVerts;
        auto srcIndices = mesh->GetPolygonVertices();
        auto srcVerts = mesh->GetControlPoints();
        mesh->GenerateNormals(args.ForceRecomputeNormal, true);
        mesh->GenerateTangentsData(0, true, false);
        int mvptr = startVertId;
        int vertexId = 0;
        List<int> vertexIdToControlPointIndex;
        List<Vec3> originalNormals;
        List<Vec3> originalVerts;
        vertexIdToControlPointIndex.SetSize(meshNumVerts);
        for (int i = 0; i < mesh->GetPolygonCount(); i++)
        {
            int matId = -1;
            if (mesh->GetPolygonSize(i) < 3)
            {
                vertexId += mesh->GetPolygonSize(i);
                continue;
            }
            if (mesh->GetElementMaterialCount() == 1)
            {
                auto elemMat = mesh->GetElementMaterial(0);
                if (elemMat->GetReferenceMode() != FbxLayerElement::eIndexToDirect)
                    throw "unsupported material element index mode";
                matId = elemMat->GetIndexArray().GetAt(i);
            }

            // we use one temp index buffer to hold vertex indices of each material
            if (!indexBuffers.ContainsKey(matId))
            {
                printf("    Subelement: %s\n", obj.mesh->GetScene()->GetMaterial(matId)->GetName());
                indexBuffers[matId] = List<int>();
            }
            auto &idxBuffer = indexBuffers[matId]();

            auto srcPolygonIndices = srcIndices + mesh->GetPolygonVertexIndex(i);
            for (auto j = 0; j < mesh->GetPolygonSize(i); j++)
            {
                int controlPointIndex = srcPolygonIndices[j];
                auto srcVert = srcVerts[srcPolygonIndices[j]];
                auto vertPos = Vec3::Create((float)srcVert[0], (float)srcVert[1], (float)srcVert[2]);
                vertPos = transformMat.TransformHomogeneous(vertPos);
                meshOut->Bounds.Union(vertPos);
                meshOut->SetVertexPosition(mvptr + j, vertPos);
                vertexIdToControlPointIndex[vertexId] = controlPointIndex;
                for (auto k = 0; k < mesh->GetElementUVCount(); k++)
                {
                    FbxGeometryElementUV *leUV = mesh->GetElementUV(k);
                    Vec2 vertUV;
                    switch (leUV->GetMappingMode())
                    {
                    default:
                        break;
                    case FbxGeometryElement::eByControlPoint:
                        switch (leUV->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertUV = GetVec2(leUV->GetDirectArray().GetAt(controlPointIndex));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int id = leUV->GetIndexArray().GetAt(controlPointIndex);
                            vertUV = GetVec2(leUV->GetDirectArray().GetAt(id));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                        break;

                    case FbxGeometryElement::eByPolygonVertex: {
                        int lTextureUVIndex = mesh->GetTextureUVIndex(i, j);
                        switch (leUV->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                        case FbxGeometryElement::eIndexToDirect: {
                            vertUV = GetVec2(leUV->GetDirectArray().GetAt(lTextureUVIndex));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                    }
                    break;

                    case FbxGeometryElement::eByPolygon: // doesn't make much sense for UVs
                    case FbxGeometryElement::eAllSame:   // doesn't make much sense for UVs
                    case FbxGeometryElement::eNone:      // doesn't make much sense for UVs
                        break;
                    }
                    meshOut->SetVertexUV(mvptr + j, k, vertUV);
                }

                for (auto k = 0; k < mesh->GetElementVertexColorCount(); k++)
                {
                    FbxGeometryElementVertexColor *leVtxc = mesh->GetElementVertexColor(k);
                    Vec4 vertColor;
                    vertColor.SetZero();
                    switch (leVtxc->GetMappingMode())
                    {
                    default:
                        break;
                    case FbxGeometryElement::eByControlPoint:
                        switch (leVtxc->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertColor = GetVec4(leVtxc->GetDirectArray().GetAt(controlPointIndex));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int id = leVtxc->GetIndexArray().GetAt(controlPointIndex);
                            vertColor = GetVec4(leVtxc->GetDirectArray().GetAt(id));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                        break;

                    case FbxGeometryElement::eByPolygonVertex: {
                        switch (leVtxc->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertColor = GetVec4(leVtxc->GetDirectArray().GetAt(vertexId));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int id = leVtxc->GetIndexArray().GetAt(vertexId);
                            vertColor = GetVec4(leVtxc->GetDirectArray().GetAt(id));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                    }
                    break;

                    case FbxGeometryElement::eByPolygon: // doesn't make much sense for UVs
                    case FbxGeometryElement::eAllSame:   // doesn't make much sense for UVs
                    case FbxGeometryElement::eNone:      // doesn't make much sense for UVs
                        break;
                    }
                    meshOut->SetVertexColor(mvptr + j, k, vertColor);
                }

                Vec3 vertNormal, vertTangent, vertBitangent;
                vertNormal.SetZero();
                vertTangent.SetZero();
                vertBitangent.SetZero();
                if (mesh->GetElementNormalCount() > 0)
                {
                    FbxGeometryElementNormal *leNormal = mesh->GetElementNormal(0);
                    if (leNormal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                    {
                        switch (leNormal->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(vertexId));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int id = leNormal->GetIndexArray().GetAt(vertexId);
                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(id));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                    }
                    else if (leNormal->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                    {
                        switch (leNormal->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(controlPointIndex));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int index = leNormal->GetIndexArray().GetAt(controlPointIndex);
                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(index));
                            break;
                        }
                        }
                    }
                }

                for (int l = 0; l < mesh->GetElementTangentCount(); ++l)
                {
                    FbxGeometryElementTangent *leTangent = mesh->GetElementTangent(l);

                    if (leTangent->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                    {
                        switch (leTangent->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertTangent = GetVec3(leTangent->GetDirectArray().GetAt(vertexId));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int id = leTangent->GetIndexArray().GetAt(vertexId);
                            vertTangent = GetVec3(leTangent->GetDirectArray().GetAt(id));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                    }
                }
                for (int l = 0; l < mesh->GetElementBinormalCount(); ++l)
                {
                    FbxGeometryElementBinormal *leBinormal = mesh->GetElementBinormal(l);

                    if (leBinormal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                    {
                        switch (leBinormal->GetReferenceMode())
                        {
                        case FbxGeometryElement::eDirect:
                            vertBitangent = GetVec3(leBinormal->GetDirectArray().GetAt(vertexId));
                            break;
                        case FbxGeometryElement::eIndexToDirect: {
                            int id = leBinormal->GetIndexArray().GetAt(vertexId);
                            vertBitangent = GetVec3(leBinormal->GetDirectArray().GetAt(id));
                        }
                        break;
                        default:
                            break; // other reference modes not shown here!
                        }
                    }
                }
                vertTangent = transformMat.TransformNormal(vertTangent);
                vertBitangent = transformMat.TransformNormal(vertBitangent);
                vertNormal = normMat.TransformNormal(vertNormal);
                Vec3 refBitangent = Vec3::Cross(vertTangent, vertNormal);
                Quaternion q = Quaternion::FromCoordinates(vertTangent, vertNormal, refBitangent);
                auto vq = q.ToVec4().Normalize();
                if (vq.w < 0)
                    vq = -vq;
                if (Vec3::Dot(refBitangent, vertTangent) < 0.0f)
                    vq = -vq;
                originalNormals.Add(vertNormal);
                originalVerts.Add(vertPos);
                meshOut->SetVertexTangentFrame(mvptr + j, vq);
                vertexId++;
            }
            for (auto j = 1; j < mesh->GetPolygonSize(i) - 1; j++)
            {
                idxBuffer.Add(mvptr);
                idxBuffer.Add(mvptr + j);
                idxBuffer.Add(mvptr + j + 1);
            }
            mvptr += mesh->GetPolygonSize(i);
        }
        
        int deformCount = mesh->GetDeformerCount(fbxsdk::FbxDeformer::eSkin);
        if (deformCount)
        {
            struct IdWeight
            {
                int boneId;
                float weight;
            };
            List<List<IdWeight>> cpWeights;
            cpWeights.SetSize(mesh->GetControlPointsCount());
            for (int i = 0; i < deformCount; i++)
            {
                auto deformer = mesh->GetDeformer(i, FbxDeformer::eSkin);
                auto skin = (::FbxSkin *)(deformer);
                for (int j = 0; j < skin->GetClusterCount(); j++)
                {
                    auto boneNode = skin->GetCluster(j)->GetLink();
                    int boneId = -1;
                    for (int k = 0; k < skeletonNodes.Count(); k++)
                        if (skeletonNodes[k] == boneNode)
                        {
                            boneId = k;
                            break;
                        }
                    auto cpIndices = skin->GetCluster(j)->GetControlPointIndices();
                    auto weights = skin->GetCluster(j)->GetControlPointWeights();
                    for (int k = 0; k < skin->GetCluster(j)->GetControlPointIndicesCount(); k++)
                    {
                        IdWeight p;
                        p.boneId = boneId;
                        p.weight = (float)weights[k];
                        cpWeights[cpIndices[k]].Add(p);
                    }
                }
            }
            // trim bone weights
            for (auto &weights : cpWeights)
            {
                weights.Sort([](IdWeight w0, IdWeight w1) { return w0.weight > w1.weight; });

                if (weights.Count() > 4)
                {
                    weights.SetSize(4);
                }
                float sumWeight = 0.0f;
                for (int k = 0; k < weights.Count(); k++)
                    sumWeight += weights[k].weight;
                sumWeight = 1.0f / sumWeight;
                for (int k = 0; k < weights.Count(); k++)
                    weights[k].weight *= sumWeight;
            }

            vertexId = 0;
            for (auto i = 0; i < mesh->GetPolygonCount(); i++)
            {
                if (mesh->GetPolygonSize(i) < 3)
                {
                    vertexId += mesh->GetPolygonSize(i);
                    continue;
                }
                auto srcPolygonIndices = srcIndices + mesh->GetPolygonVertexIndex(i);
                for (auto j = 0; j < mesh->GetPolygonSize(i); j++)
                {
                    int cpId = srcPolygonIndices[j];
                    Array<int, 8> boneIds;
                    Array<float, 8> boneWeights;
                    boneIds.SetSize(4);
                    boneWeights.SetSize(4);
                    boneIds[0] = boneIds[1] = boneIds[2] = boneIds[3] = -1;
                    boneWeights[0] = boneWeights[1] = boneWeights[2] = boneWeights[3] = 0.0f;
                    for (int k = 0; k < cpWeights[cpId].Count(); k++)
                    {
                        boneIds[k] = (cpWeights[cpId][k].boneId);
                        boneWeights[k] = (cpWeights[cpId][k].weight);
                    }
                    meshOut->SetVertexSkinningBinding(
                        startVertId + vertexId, boneIds.GetArrayView(), boneWeights.GetArrayView());
                    vertexId++;
                }
            }
        }
        else if (hasBones)
        {
            Array<int, 4> boneIds;
            Array<float, 4> boneWeights;
            boneIds.SetSize(4);
            boneWeights.SetSize(4);
            boneIds[0] = 0;
            for (int i = 1; i < boneIds.GetCapacity(); i++)
                boneIds[i] = 255;
            boneWeights[0] = 1.0f;
            for (int i = 1; i < boneIds.GetCapacity(); i++)
                boneWeights[i] = 0.0f;
            for (auto i = 0; i < vertexId; i++)
                meshOut->SetVertexSkinningBinding(startVertId + i, boneIds.GetArrayView(), boneWeights.GetArrayView());
        }

        int blendShapeCount = mesh->GetDeformerCount(fbxsdk::FbxDeformer::eBlendShape);
        List<BlendShapeChannel> blendshapeChannels;
        meshOut->ElementBlendShapeChannels.SetSize(meshOut->ElementRanges.Count());
        if (blendShapeCount)
        {
            for (int i = 0; i < blendShapeCount; i++)
            {
                auto fbxBlendShape = (fbxsdk::FbxBlendShape*)(mesh->GetDeformer(i, fbxsdk::FbxDeformer::eBlendShape));
                blendshapeChannels.SetSize(fbxBlendShape->GetBlendShapeChannelCount());
                for (int channelId = 0; channelId < fbxBlendShape->GetBlendShapeChannelCount(); channelId++)
                {
                    auto fbxBlendShapeChannel = (fbxsdk::FbxBlendShapeChannel*)(fbxBlendShape->GetBlendShapeChannel(channelId));
                    auto &blendShapeChannelOut = blendshapeChannels[channelId];
                    blendShapeChannelOut.Name = RemoveNamespace(fbxBlendShapeChannel->GetNameWithoutNameSpacePrefix().Buffer(), args.RemoveNamespace);
                    blendShapeChannelOut.ChannelId = blendShapeChannelCount;
                    blendShapeChannelCount++;
                    blendShapeChannelOut.BlendShapes.SetSize(fbxBlendShapeChannel->GetTargetShapeCount());
                    for (int j = 0; j < fbxBlendShapeChannel->GetTargetShapeCount(); j++)
                    {
                        auto &blendShapeOut = blendShapeChannelOut.BlendShapes[j];
                        blendShapeOut.BlendShapeVertexStartIndex = meshOut->BlendShapeVertices.Count() - startVertId;
                        blendShapeOut.FullWeightPercentage = (float)fbxBlendShapeChannel->GetTargetShapeFullWeights()[j];
                        auto fbxTargetShape = fbxBlendShapeChannel->GetTargetShape(j);
                        int bsVertId = 0;
                        CORELIB_ASSERT(fbxTargetShape->GetControlPointsCount() == mesh->GetControlPointsCount());
                        auto shapeControlPoints = fbxTargetShape->GetControlPoints();
                        auto shapeIndices = mesh->GetPolygonVertices();
                        auto leNormal = fbxTargetShape->GetElementNormal(0);
                        for (auto f = 0; f < mesh->GetPolygonCount(); f++)
                        {
                            auto srcPolygonIndices = shapeIndices + mesh->GetPolygonVertexIndex(f);
                            for (auto k = 0; k < mesh->GetPolygonSize(f); k++)
                            {
                                int controlPointIndex = srcPolygonIndices[k];
                                auto srcVert = shapeControlPoints[srcPolygonIndices[k]];
                                auto vertPos = Vec3::Create((float)srcVert[0], (float)srcVert[1], (float)srcVert[2]);
                                vertPos = transformMat.TransformHomogeneous(vertPos);
                                VectorMath::Vec3 vertNormal = originalNormals[bsVertId];

                                if (!args.NoBlendShapeNormals && leNormal)
                                {
                                    if (leNormal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
                                    {
                                        switch (leNormal->GetReferenceMode())
                                        {
                                        case FbxGeometryElement::eDirect:
                                            vertNormal =
                                                GetVec3(leNormal->GetDirectArray().GetAt(bsVertId));
                                            break;
                                        case FbxGeometryElement::eIndexToDirect: {
                                            int id = leNormal->GetIndexArray().GetAt(bsVertId);
                                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(id));
                                        }
                                        break;
                                        default:
                                            break; // other reference modes not shown here!
                                        }
                                    }
                                    else if (leNormal->GetMappingMode() == FbxGeometryElement::eByControlPoint)
                                    {
                                        switch (leNormal->GetReferenceMode())
                                        {
                                        case FbxGeometryElement::eDirect:
                                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(controlPointIndex));
                                            break;
                                        case FbxGeometryElement::eIndexToDirect: {
                                            int index = leNormal->GetIndexArray().GetAt(controlPointIndex);
                                            vertNormal = GetVec3(leNormal->GetDirectArray().GetAt(index));
                                            break;
                                        }
                                        }
                                    }
                                    vertNormal = normMat.TransformNormal(vertNormal);
                                }
                                auto deltaNormal = vertNormal - originalNormals[bsVertId];
                                int quantizedNormal[3] = {Math::Clamp((int)((deltaNormal.x + 1.0f) * 511.5f), 0, 1023),
                                    Math::Clamp((int)((deltaNormal.y + 1.0f) * 511.5f), 0, 1023),
                                    Math::Clamp((int)((deltaNormal.z + 1.0f) * 511.5f), 0, 1023)};
                                BlendShapeVertex bsVert;
                                bsVert.DeltaPosition = vertPos - originalVerts[bsVertId];
                                bsVert.Normal =
                                    (quantizedNormal[0]) + (quantizedNormal[1] << 10) + (quantizedNormal[2] << 20);
                                meshOut->BlendShapeVertices.Add(bsVert);
                                bsVertId++;
                            }
                        }
                    }
                }
            }
        }

        // merge index buffers and fill in element ranges
        for (auto &idxBuf : indexBuffers)
        {
            matIndices.Add(idxBuf.Key);
            MeshElementRange range;
            range.StartIndex = meshOut->Indices.Count();
            meshOut->Indices.AddRange(idxBuf.Value);
            range.Count = idxBuf.Value.Count();
            meshOut->ElementRanges.Add(range);
            meshOut->ElementBlendShapeChannels.Add(blendshapeChannels);
        }
        startVertId += vertexId;
    }
    Mesh optimizedMesh;
    if (meshOut->ElementBlendShapeChannels.Count())
    {
        optimizedMesh = *meshOut; // do not optimize if we have blend shapes.
    }
    else
    {
        optimizedMesh = meshOut->DeduplicateVertices();
        Mesh lightmappedMesh;
        GenerateLightmapUV(&lightmappedMesh, &optimizedMesh, 1024, 6);
        optimizedMesh = _Move(lightmappedMesh);
    }
    wprintf(L"mesh converted: elements %d, faces: %d, vertices: %d, skeletal: %s, blendshapes %s.\n",
        optimizedMesh.ElementRanges.Count(), optimizedMesh.Indices.Count() / 3, optimizedMesh.GetVertexCount(),
        hasBones ? L"true" : L"false",
        optimizedMesh.BlendShapeVertices.Count() ? L"true" : L"false");
    return optimizedMesh;
}

void ExportLevel(List<SceneStaticObject> &objs, ExportArguments &args, String fileName)
{
    StringBuilder levelSb;
    levelSb << R"(
Camera
{
    Name "FreeCam"
    CastShadow true
    RenderCustomDepth false
    LocalTransform [ 0.089711972 -9.3132269e-10 0.99596786 -0 0.11230501 0.99362236 -0.010115892 0 -0.98961586 0.11275969 0.089139819 -0 -460.38934 105.47602 41.378948 1 ]
    Orientation [ 1.4809636 0.113 9.3736874e-10 ]
    Position [ -460.38928 105.47601 41.378944 ]
    ZNear 5
    ZFar 3000
    FOV 60
    Radius 50
}
ArcBallCameraController
{
    Name "ArcBallCameraController0"
    CastShadow true
    RenderCustomDepth false
    LocalTransform [ 1 0 0 0 0 1 0 0 0 0 1 0 0 0 -150 1 ]
    Center [-16.668 55.730 -71.031]
    Radius 410.527
    Alpha 2.227
    Beta 0.091
    TurnPrecision 0.0031415927
    TranslatePrecision 0.001
    ZoomScale 1.1
    MinDist 10
    MaxDist 10000
    NeedAlt true
    TargetCameraName "FreeCam"
}
DirectionalLight
{
    Name "sunlight"
    CastShadow true
    RenderCustomDepth false
    LocalTransform [ 0.28457481 -0.30867925 0.86600453 0 -0.71176046 0.55429089 0.43146098 0 0.6132015 0.73917073 0.061968461 0 298.37256 168.03613 318.39752 1 ]
    EnableCascadedShadows true
    ShadowDistance 4500
    NumShadowCascades 8
    TransitionFactor 0.80000001
    Color [ 2.4000001 2.3 2.0999999 ]
    Ambient 0
}
AmbientLight
{
    Name "AmbientLight0"
    CastShadow true
    RenderCustomDepth false
    LocalTransform [ 1 0 0 0 0 1 0 0 0 0 1 0 5.0134735 152.85149 157.49091 1 ]
    Ambient [ 0.5 0.30000001 0.2 ]
}
Atmosphere
{
    Name "atmosphere"
    CastShadow true
    RenderCustomDepth false
    LocalTransform [ 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 ]
    SunDir [ 0.72428602 0.6337502 0.27160725 ]
    AtmosphericFogScaleFactor 0.30000001
}
)";
    Dictionary<FbxMesh *, String> meshFileNames;
    HashSet<String> existingNames, existingMeshNames;
    int counter = 0;
    for (auto &obj : objs)
    {
        // if we haven't exported this mesh, export it now
        if (!meshFileNames.ContainsKey(obj.mesh))
        {
            String meshName = obj.name;
            if (meshName.Length() == 0)
                meshName = "static_mesh";
            int c = 0;
            auto newMeshName = meshName;
            while (existingMeshNames.Contains(newMeshName))
            {
                newMeshName = meshName + c;
                c++;
            }
            existingMeshNames.Add(newMeshName);
            auto meshFile = newMeshName + ".mesh";
            meshFileNames[obj.mesh] = meshFile;
            List<SceneStaticObject> singleObj;
            singleObj.SetSize(1);
            singleObj[0].mesh = obj.mesh;
            singleObj[0].name = newMeshName;
            Matrix4::CreateIdentityMatrix(singleObj[0].transform);
            List<int> materialIds;
            auto mesh = ExportMesh(singleObj, args, List<fbxsdk::FbxNode *>(), materialIds);
            mesh.SaveToFile(Path::Combine(Path::GetDirectoryName(fileName), Path::GetFileName(meshFile)));
            StringBuilder modelSB;
            modelSB << "model\n{\n\tmesh \"" << args.MeshPathPrefix << meshFile << "\"\n";
            for (auto mid : materialIds)
            {
                if (mid != -1)
                    modelSB << "\tmaterial \"" << args.MeshPathPrefix << obj.node->GetMaterial(mid)->GetName()
                            << ".material\"\n";
                else
                    modelSB << "\tmaterial \"Default.material\"\n";
            }
            modelSB << "}";
            File::WriteAllText(
                Path::ReplaceExt(Path::Combine(Path::GetDirectoryName(fileName), Path::GetFileName(meshFile)), "model"),
                modelSB.ProduceString());
            // now export actual materials
            auto extractTexture = [](fbxsdk::FbxSurfaceMaterial *mat, const char *propName) -> const char * {
                FbxProperty prop = mat->FindProperty(propName);
                int layeredTextureCount = prop.GetSrcObjectCount<FbxLayeredTexture>();
                int fileTextureCount = prop.GetSrcObjectCount<FbxFileTexture>();
                if (fileTextureCount)
                {
                    return ((FbxFileTexture *)prop.GetSrcObject<FbxFileTexture>(0))->GetFileName();
                }
                return nullptr;
            };
            for (int i = 0; i < obj.node->GetMaterialCount(); i++)
            {
                StringBuilder materialSb;
                auto mat = obj.node->GetMaterial(i);
                String diffuseMap = extractTexture(mat, fbxsdk::FbxSurfaceMaterial::sDiffuse);
                String specularMap = extractTexture(mat, fbxsdk::FbxSurfaceMaterial::sSpecular);
                String normalMap = extractTexture(mat, fbxsdk::FbxSurfaceMaterial::sBump);
                if (!diffuseMap.Length())
                    diffuseMap = "default_diffuse.texture";
                else
                    diffuseMap = args.MeshPathPrefix + Path::GetFileName(diffuseMap);
                if (!specularMap.Length())
                    specularMap = "black.texture";
                else
                    specularMap = args.MeshPathPrefix + Path::GetFileName(specularMap);
                if (!normalMap.Length())
                    normalMap = "default_normal.texture";
                else
                    normalMap = args.MeshPathPrefix + Path::GetFileName(normalMap);

                materialSb << "material\n{\n\tshader \"StandardFbx.shader\"\n\tvar albedoMap = texture[\"" << diffuseMap
                           << "\"]\n\tvar specularMap = texture[\"" << specularMap
                           << "\"]\n\tvar normalMap = texture[\"" << normalMap
                           << "\"]\n\tvar pRoughness = float[0.7]\n\tvar pMetallic = float[0.3]\n}";
                File::WriteAllText(
                    Path::Combine(Path::GetDirectoryName(fileName), String(mat->GetName()) + ".material"),
                    materialSb.ProduceString());
            }
        }
        if (obj.name.Length() == 0 || existingNames.Contains(obj.name))
        {
            if (obj.name.Length() == 0)
                obj.name = String("static_mesh_") + counter;
            else
                obj.name = obj.name + "_dedup_" + counter;
            counter++;
        }
        existingNames.Add(obj.name);
        // export reference in level file
        levelSb << "StaticMesh\n{\n\tName \"" << obj.name << "\"\n\t";
        levelSb << "ModelFile \"" << args.MeshPathPrefix << Path::ReplaceExt(meshFileNames[obj.mesh](), "model")
                << "\"\n\t";
        levelSb << "LocalTransform [";
        for (int i = 0; i < 16; i++)
            levelSb << obj.transform.values[i] << " ";
        levelSb << "]\n}\n";
    }
    File::WriteAllText(fileName, levelSb.ProduceString());
}

void Export(ExportArguments args)
{
    if (args.FlipYZ)
    {
        Quaternion flipYZTransform = Quaternion::FromAxisAngle(Vec3::Create(1.0f, 0.0f, 0.0f), Math::Pi * 0.5f);
        args.RootTransform = flipYZTransform * args.RootTransform;
    }

    RootTransformApplier rootTransformApplier(args.RootTransform);
    Matrix4 rootFixTransform = args.RootFixTransform.ToMatrix4();

    auto fileName = args.FileName;
    auto outFileName = Path::TruncateExt(fileName) + args.FileNameSuffix + ".out";

    FbxManager *lSdkManager = FbxManager::Create();
    FbxIOSettings *ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
    lSdkManager->SetIOSettings(ios);

    // Create an importer using the SDK manager.
    ::FbxImporter *lImporter = ::FbxImporter::Create(lSdkManager, "");

    // Use the first argument as the filename for the importer.
    if (!lImporter->Initialize(fileName.Buffer(), -1, lSdkManager->GetIOSettings()))
    {
        printf("Call to FbxImporter::Initialize() failed.\n");
        printf("Error returned: %s\n\n", lImporter->GetStatus().GetErrorString());
        return;
    }
    // Create a new scene so that it can be populated by the imported file.
    FbxScene *lScene = FbxScene::Create(lSdkManager, "myScene");

    // Import the contents of the file into the scene.
    lImporter->Import(lScene);

    // The file is imported, so get rid of the importer.
    lImporter->Destroy();

    // Print the nodes of the scene and their attributes recursively.
    // Note that we are not printing the root node because it should
    // not contain any attributes.
    fbxsdk::FbxNode *lRootNode = lScene->GetRootNode();

    Skeleton skeleton;
    skeleton.Name = Path::GetFileNameWithoutEXT(args.FileName);
    List<fbxsdk::FbxNode *> skeletonNodes;
    if (args.ExportSkeleton || args.ExportAnimation)
    {
        // find root bone
        if (args.RootNodeName.Length() == 0)
        {
            bool rootBoneFound = false;
            // find root bone
            auto skeletonRootNode = FindFirstSkeletonNode(lRootNode);
            if (skeletonRootNode)
                args.RootNodeName = skeletonRootNode->GetName();
            else
            {
                printf("error: root bone not specified.\n");
                goto endOfSkeletonExport;
            }
        }
        auto firstBoneNode = lRootNode->FindChild(args.RootNodeName.Buffer());
        if (!firstBoneNode)
        {
            printf("error: root node named '%s' not found.\n", args.RootNodeName.Buffer());
            goto endOfSkeletonExport;
        }
        auto rootBoneNode = firstBoneNode;

        if (args.RemoveNamespace)
        {
            String rootName = RemoveNamespace(rootBoneNode->GetName(), args.RemoveNamespace);
            printf("root bone is \'%s\'\n", rootName.Buffer());
        }

        GetSkeletonNodes(skeletonNodes, rootBoneNode);
        skeleton.Bones.SetSize(skeletonNodes.Count());
        skeleton.InversePose.SetSize(skeleton.Bones.Count());

        // Populate bone names and parentId.
        for (auto i = 0; i < skeletonNodes.Count(); i++)
        {
            skeleton.Bones[i].Name = RemoveNamespace(skeletonNodes[i]->GetName(), args.RemoveNamespace);
            skeleton.Bones[i].ParentId = -1;
            skeleton.BoneMapping[skeleton.Bones[i].Name] = i;
        }
        for (auto i = 0; i < skeletonNodes.Count(); i++)
        {
            if (skeletonNodes[i])
            {
                for (auto j = 0; j < skeletonNodes[i]->GetChildCount(); j++)
                {
                    int boneId = -1;
                    String childName = RemoveNamespace(skeletonNodes[i]->GetChild(j)->GetName(), args.RemoveNamespace);
                    if (skeleton.BoneMapping.TryGetValue(childName, boneId))
                    {
                        skeleton.Bones[boneId].ParentId = i;
                    }
                }
            }
        }

        // For each bone, find its BindPose matrix from FbxScene::Poses
        // An fbxscene contains multiple poses (one pose for each object),
        // and the bind pose matrix for a skeleton node must be available from
        // at least one of these poses.
        auto lPoseCount = lScene->GetPoseCount();
        for (int n = 0; n < skeletonNodes.Count(); n++)
        {
            auto node = skeletonNodes[n];
            for (int i = 0; i < lPoseCount; i++)
            {
                auto *lPose = lScene->GetPose(i);
                for (int j = 0; j < lPose->GetCount(); j++)
                {
                    if (lPose->GetNode(j) == node)
                    {
                        // We find the bind pose for this node.
                        auto matrix = GetMatrix(lPose->GetMatrix(j));
                        skeleton.Bones[n].BindPose.FromMatrix(matrix);
                        goto nextN;
                    }
                }
            }
            // The bind pose is not defined in any of the poses.
            printf("Warning: bind pose for %s is not defined, default to identity.\n", node->GetName());
        nextN:;
        }
        // The bind pose matrix retrieved from fbx are global transforms
        // Convert them to local coordinates here.
        for (int n = skeletonNodes.Count() - 1; n >= 0; n--)
        {
            auto globalMat = skeleton.Bones[n].BindPose.ToMatrix();
            int parentId = skeleton.Bones[n].ParentId;
            if (parentId != -1)
            {
                Matrix4 invParentMat, local;
                skeleton.Bones[parentId].BindPose.ToMatrix().Inverse(invParentMat);
                Matrix4::Multiply(local, invParentMat, globalMat);
                skeleton.Bones[n].BindPose.FromMatrix(local);
            }
        }

        // apply root fix transform
        skeleton.Bones[0].BindPose = RootTransformApplier::ApplyFix(rootFixTransform, skeleton.Bones[0].BindPose);

        // apply root transform
        for (auto &bone : skeleton.Bones)
            bone.BindPose = rootTransformApplier.Apply(bone.BindPose);

        // compute inverse matrices
        for (auto i = 0; i < skeletonNodes.Count(); i++)
        {
            skeleton.InversePose[i] = skeleton.Bones[i].BindPose.ToMatrix();
        }
        for (auto i = 0; i < skeletonNodes.Count(); i++)
        {
            if (skeleton.Bones[i].ParentId != -1)
                Matrix4::Multiply(
                    skeleton.InversePose[i], skeleton.InversePose[skeleton.Bones[i].ParentId], skeleton.InversePose[i]);
        }
        for (auto i = 0; i < skeletonNodes.Count(); i++)
            skeleton.InversePose[i].Inverse(skeleton.InversePose[i]);
        if (args.ExportSkeleton)
        {
            skeleton.SaveToFile(Path::ReplaceExt(outFileName, "skeleton"));
            printf("skeleton converted. total bones: %d.\n", skeleton.Bones.Count());
            if (args.CreateMeshFromSkeleton)
            {
                Mesh m;
                m.FromSkeleton(&skeleton, 5.0f);
                m.SaveToFile(Path::ReplaceExt(outFileName, "skeleton.mesh"));
            }
        }
    }

endOfSkeletonExport:
    List<SceneStaticObject> staticObjects;
    Procedure<fbxsdk::FbxNode *> visitNode = [&](fbxsdk::FbxNode *node) {
        for (auto cid = 0; cid < node->GetChildCount(); cid++)
        {
            auto child = node->GetChild(cid);
            if (String(child->GetName()).Contains(args.IgnorePattern))
                continue;
            if (auto mesh = child->GetMesh())
            {
                SceneStaticObject obj;
                auto globalTransform = child->EvaluateGlobalTransform();
                obj.transform = GetMatrix(globalTransform);
                obj.mesh = mesh;
                obj.name = child->GetName();
                obj.node = child;
                staticObjects.Add(obj);
            }
            else
            {
                visitNode(node->GetChild(cid));
            }
        }
    };
    visitNode(lRootNode);

    if (args.ExportMesh || args.ExportScene)
    {
        int numColorChannels = 0;
        int numUVChannels = 0;
        int numVerts = 0;
        int numFaces = 0;
        bool hasBones = false;
        
        if (!args.ExportScene)
        {
            List<int> materialIndices;
            auto mesh = ExportMesh(staticObjects, args, skeletonNodes, materialIndices);
            mesh.SaveToFile(Path::ReplaceExt(outFileName, "mesh"));
        }
        else
        {
            ExportLevel(staticObjects, args, Path::ReplaceExt(outFileName, "level"));
        }
    }

    // export animation
    if (args.ExportAnimation)
    {
        FbxAnimStack *currAnimStack = lScene->GetCurrentAnimationStack();
        SkeletalAnimation anim;
        if (!currAnimStack)
        {
            printf("no animation stack is found.\n");
            return;
        }
        fbxsdk::FbxAnimLayer *animLayer = currAnimStack->GetMember<fbxsdk::FbxAnimLayer>(0);
        if (!animLayer)
        {
            printf("no animation layer is found.\n");
            return;
        }
        printf("Converting animation...\n");
        FbxString animStackName = currAnimStack->GetName();
        char *mAnimationName = animStackName.Buffer();
        FbxTakeInfo *takeInfo = lScene->GetTakeInfo(animStackName);
        fbxsdk::FbxTime start = takeInfo->mLocalTimeSpan.GetStart();
        fbxsdk::FbxTime end = takeInfo->mLocalTimeSpan.GetStop();
        if (skeletonNodes.Count())
        {
            bool inconsistentKeyFrames = false;

            anim.Speed = 1.0f;
            anim.FPS = 30.0f;
            anim.Name = Split(fileName, '.')[0];
            anim.Channels.SetSize(skeletonNodes.Count());
            memset(anim.Reserved, 0, sizeof(anim.Reserved));
            int channelId = 0;
            for (int boneId = 0; boneId < skeletonNodes.Count(); boneId++)
            {
                auto inNode = skeletonNodes[boneId];
                anim.Channels[channelId].BoneName = RemoveNamespace(inNode->GetName(), args.RemoveNamespace);

                bool isRoot = anim.Channels[channelId].BoneName == args.RootNodeName;
                unsigned int ptrPos = 0, ptrRot = 0, ptrScale = 0;
                int frameCounter = 0;
                fbxsdk::FbxAnimCurve *rxCurve = inNode->LclRotation.GetCurve(animLayer, "X", true);
                fbxsdk::FbxAnimCurve *ryCurve = inNode->LclRotation.GetCurve(animLayer, "Y", true);
                fbxsdk::FbxAnimCurve *rzCurve = inNode->LclRotation.GetCurve(animLayer, "Z", true);

                fbxsdk::FbxAnimCurve *txCurve = inNode->LclTranslation.GetCurve(animLayer, "X", true);
                fbxsdk::FbxAnimCurve *tyCurve = inNode->LclTranslation.GetCurve(animLayer, "Y", true);
                fbxsdk::FbxAnimCurve *tzCurve = inNode->LclTranslation.GetCurve(animLayer, "Z", true);

                for (FbxLongLong i = start.GetFrameCount(fbxsdk::FbxTime::eFrames30);
                     i <= end.GetFrameCount(fbxsdk::FbxTime::eFrames30); ++i)
                {
                    fbxsdk::FbxTime currTime;
                    currTime.SetFrame(i, fbxsdk::FbxTime::eFrames30);
                    if (!takeInfo->mLocalTimeSpan.IsInside(currTime))
                    {
                        if (frameCounter > 0)
                            break;
                        else
                            continue;
                    }

                    Vec3 r = Vec3::Create(0.f);
                    r[0] = rxCurve->Evaluate(currTime) / 180.0f * PI;
                    r[1] = ryCurve->Evaluate(currTime) / 180.0f * PI;
                    r[2] = rzCurve->Evaluate(currTime) / 180.0f * PI;

                    Vec3 t = Vec3::Create(0.f);
                    t[0] = txCurve->Evaluate(currTime);
                    t[1] = tyCurve->Evaluate(currTime);
                    t[2] = tzCurve->Evaluate(currTime);

                    AnimationKeyFrame keyFrame;
                    keyFrame.Time =
                        (float)(frameCounter / 30.0f); // Reset the time considering we may want to clip the animation
                    // keyFrame.Transform.Translation = t;
                    // EulerAngleToQuaternion(keyFrame.Transform.Rotation, r[0], r[1], r[2], EulerAngleOrder::XYZ);
                    keyFrame.Transform.FromMatrix(GetMatrix(inNode->EvaluateLocalTransform(currTime)));

                    // for debug
                    Vec3 t1 = keyFrame.Transform.Translation;
                    Quaternion r2 = keyFrame.Transform.Rotation;
                    float rx, ry, rz;
                    QuaternionToEulerAngle(r2, rx, ry, rz, EulerAngleOrder::XYZ);

                    // apply root fix transform
                    keyFrame.Transform = RootTransformApplier::ApplyFix(rootFixTransform, keyFrame.Transform);
                    keyFrame.Transform = rootTransformApplier.Apply(keyFrame.Transform);
                    anim.Channels[channelId].KeyFrames.Add(keyFrame);
                    frameCounter++;
                }
                anim.Duration = frameCounter / 30.0f; // unit: second
                channelId++;
            }
        }
        for (auto &obj : staticObjects)
        {
            for (int i = 0; i < obj.mesh->GetDeformerCount(fbxsdk::FbxDeformer::EDeformerType::eBlendShape); i++)
            {
                auto blendshape = ((fbxsdk::FbxBlendShape *)obj.mesh->GetDeformer(
                    i, fbxsdk::FbxDeformer::EDeformerType::eBlendShape));
                for (int j = 0; j < blendshape->GetBlendShapeChannelCount(); j++)
                {
                    BlendShapeAnimationChannel animChannel;
                    animChannel.Name = RemoveNamespace(blendshape->GetBlendShapeChannel(j)->GetNameWithoutNameSpacePrefix().Buffer(), args.RemoveNamespace);
                    auto curve = blendshape->GetBlendShapeChannel(j)->DeformPercent.GetCurve(animLayer);
                    if (!curve)
                        continue;
                    int frameCounter = 0;
                    int64_t startFrame = start.GetFrameCount(fbxsdk::FbxTime::eFrames30);
                    int64_t endFrame = end.GetFrameCount(fbxsdk::FbxTime::eFrames30);
                    for (FbxLongLong f = startFrame; f <= endFrame; ++f)
                    {
                        fbxsdk::FbxTime currTime;
                        currTime.SetFrame(f, fbxsdk::FbxTime::eFrames30);
                        if (!takeInfo->mLocalTimeSpan.IsInside(currTime))
                        {
                            if (frameCounter > 0)
                                break;
                            else
                                continue;
                        }
                        BlendShapeAnimationKeyFrame keyFrame;
                        keyFrame.Time = (float)frameCounter / 30.0f;
                        keyFrame.Weight = curve->Evaluate(currTime);
                        animChannel.KeyFrames.Add(keyFrame);
                        frameCounter++;
                    }
                    anim.BlendShapeChannels.Add(_Move(animChannel));
                }
            }
        }
        anim.SaveToFile(Path::ReplaceExt(outFileName, "anim"));
        int maxKeyFrames = 0;
        for (auto &c : anim.Channels)
            maxKeyFrames = Math::Max(maxKeyFrames, c.KeyFrames.Count());
        printf("animation converted. keyframes %d, bones %d, blendshape channels %d\n", maxKeyFrames,
            anim.Channels.Count(), anim.BlendShapeChannels.Count());
    }

    // Destroy the SDK manager and all the other objects it was handling.
    lSdkManager->Destroy();
}

/*
What is RootFixTransform?

When applied to animation and skeleton, RootFixTransform "cancels" the specified a wrong coordinate system
transformation of the input data, by applying the specified transform to the root node.
*/

class ModelImporterForm : public Form
{
private:
    RefPtr<Button> btnSelectFiles;
    RefPtr<CheckBox> chkExportLevel, chkFlipYZ, chkFlipUV, chkFlipWinding, chkCreateSkeletonMesh, chkRemoveNamespace,
        chkForceRecomputeNormal, chkNoBlendShapeNormals;
    RefPtr<TextBox> txtRootTransform, txtRootFixTransform, txtRootBoneName, txtSuffix, txtMeshPathPrefix, txtIgnoreNamePattern;
    RefPtr<Label> lblRootTransform, lblRootFixTransform, lblRootBoneName, lblSuffix, lblMeshPathPrefix, lblIgnoreNamePattern;
    Quaternion ParseRootTransform(String txt)
    {
        if (txt.Trim() == "")
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        Quaternion rs(0.0f, 0.0f, 0.0f, 1.0f);
        Text::TokenReader parser(txt);
        try
        {
            while (!parser.IsEnd())
            {
                if (parser.LookAhead("rotx"))
                {
                    parser.ReadToken();
                    rs = rs * Quaternion::FromAxisAngle(
                                  Vec3::Create(1.0f, 0.0f, 0.0f), parser.ReadFloat() / 180.0f * Math::Pi);
                }
                else if (parser.LookAhead("roty"))
                {
                    parser.ReadToken();
                    rs = rs * Quaternion::FromAxisAngle(
                                  Vec3::Create(0.0f, 1.0f, 0.0f), parser.ReadFloat() / 180.0f * Math::Pi);
                }
                else if (parser.LookAhead("rotz"))
                {
                    parser.ReadToken();
                    rs = rs * Quaternion::FromAxisAngle(
                                  Vec3::Create(0.0f, 0.0f, 1.0f), parser.ReadFloat() / 180.0f * Math::Pi);
                }
                else
                    throw Text::TextFormatException("");
            }
        }
        catch (const Exception &)
        {
            MessageBox("Invalid root transform.", "Error", MB_ICONEXCLAMATION);
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return rs;
    }

public:
    ModelImporterForm()
    {
        SetText("Convert FBX");
        SetClientWidth(400);
        SetClientHeight(420);
        SetMaximizeBox(false);
        SetBorder(fbFixedDialog);
        chkExportLevel = new CheckBox(this);
        chkExportLevel->SetPosition(200, 20, 180, 25);
        chkExportLevel->SetText("Export Level");
        chkExportLevel->SetChecked(ExportArguments().ExportScene);

        chkForceRecomputeNormal = new CheckBox(this);
        chkForceRecomputeNormal->SetPosition(200, 50, 180, 25);
        chkForceRecomputeNormal->SetText("Force recomputed normal");
        chkForceRecomputeNormal->SetChecked(ExportArguments().ForceRecomputeNormal);

        chkNoBlendShapeNormals = new CheckBox(this);
        chkNoBlendShapeNormals->SetPosition(200, 80, 180, 25);
        chkNoBlendShapeNormals->SetText("Disable blendshape normal");
        chkNoBlendShapeNormals->SetChecked(ExportArguments().NoBlendShapeNormals);

        chkFlipYZ = new CheckBox(this);
        chkFlipYZ->SetPosition(50, 20, 80, 25);
        chkFlipYZ->SetText("Flip YZ");
        chkFlipYZ->SetChecked(ExportArguments().FlipYZ);

        chkFlipUV = new CheckBox(this);
        chkFlipUV->SetPosition(50, 50, 120, 25);
        chkFlipUV->SetText("Flip UV");
        chkFlipUV->SetChecked(ExportArguments().FlipUV);
        chkFlipWinding = new CheckBox(this);
        chkFlipWinding->SetPosition(50, 80, 120, 25);
        chkFlipWinding->SetText("Flip Winding");
        chkFlipWinding->SetChecked(ExportArguments().FlipWindingOrder);
        chkCreateSkeletonMesh = new CheckBox(this);
        chkCreateSkeletonMesh->SetPosition(50, 110, 180, 25);
        chkCreateSkeletonMesh->SetText("Create Mesh From Skeleton");
        chkCreateSkeletonMesh->SetChecked(ExportArguments().CreateMeshFromSkeleton);

        chkRemoveNamespace = new CheckBox(this);
        chkRemoveNamespace->SetPosition(50, 140, 200, 25);
        chkRemoveNamespace->SetText("Remove Namespace");
        chkRemoveNamespace->SetChecked(ExportArguments().RemoveNamespace);

        txtRootBoneName = new TextBox(this);
        txtRootBoneName->SetText("");
        txtRootBoneName->SetPosition(200, 170, 100, 25);
        lblRootBoneName = new Label(this);
        lblRootBoneName->SetText("Root Name:");
        lblRootBoneName->SetPosition(50, 175, 100, 25);

        txtRootTransform = new TextBox(this);
        txtRootTransform->SetPosition(200, 200, 100, 25);
        txtRootTransform->SetText("");
        lblRootTransform = new Label(this);
        lblRootTransform->SetText("Root Transform: ");
        lblRootTransform->SetPosition(50, 205, 100, 25);

        txtRootFixTransform = new TextBox(this);
        txtRootFixTransform->SetPosition(200, 230, 100, 25);
        txtRootFixTransform->SetText("");
        lblRootFixTransform = new Label(this);
        lblRootFixTransform->SetText("Root Fix:");
        lblRootFixTransform->SetPosition(50, 235, 100, 25);

        txtSuffix = new TextBox(this);
        txtSuffix->SetPosition(200, 260, 100, 25);
        txtSuffix->SetText("");
        lblSuffix = new Label(this);
        lblSuffix->SetText("Filename Suffix: ");
        lblSuffix->SetPosition(50, 265, 100, 25);

        txtMeshPathPrefix = new TextBox(this);
        txtMeshPathPrefix->SetPosition(200, 290, 100, 25);
        txtMeshPathPrefix->SetText("");
        lblMeshPathPrefix = new Label(this);
        lblMeshPathPrefix->SetText("Mesh Path Prefix: ");
        lblMeshPathPrefix->SetPosition(50, 295, 100, 25);

        lblIgnoreNamePattern = new Label(this);
        lblIgnoreNamePattern->SetPosition(50, 325, 100, 25);
        lblIgnoreNamePattern->SetText("Ignore Pattern:");
        txtIgnoreNamePattern = new TextBox(this);
        txtIgnoreNamePattern->SetText("_Blendshapes");
        txtIgnoreNamePattern->SetPosition(200, 320, 100, 25);

        btnSelectFiles = new Button(this);
        btnSelectFiles->SetPosition(60, 360, 120, 30);
        btnSelectFiles->SetText("Select Files");
        btnSelectFiles->OnClick.Bind([=](Object *, EventArgs) {
            FileDialog dlg(this);
            dlg.MultiSelect = true;
            if (dlg.ShowOpen())
            {
                for (auto file : dlg.FileNames)
                {
                    ExportArguments args;
                    args.FlipUV = chkFlipUV->GetChecked();
                    args.FlipYZ = chkFlipYZ->GetChecked();
                    args.FlipWindingOrder = chkFlipWinding->GetChecked();
                    args.ForceRecomputeNormal = chkForceRecomputeNormal->GetChecked();
                    args.NoBlendShapeNormals = chkNoBlendShapeNormals->GetChecked();
                    args.FileName = file;
                    args.RootNodeName = txtRootBoneName->GetText();
                    args.RootTransform = ParseRootTransform(txtRootTransform->GetText());
                    args.RootFixTransform = ParseRootTransform(txtRootFixTransform->GetText());
                    args.FileNameSuffix = txtSuffix->GetText();
                    args.RemoveNamespace = chkRemoveNamespace->GetChecked();
                    args.ExportScene = chkExportLevel->GetChecked();
                    args.MeshPathPrefix = txtMeshPathPrefix->GetText();
                    args.CreateMeshFromSkeleton = chkCreateSkeletonMesh->GetChecked();
                    args.IgnorePattern = txtIgnoreNamePattern->GetText();
                    Export(args);
                }
            }
        });
    }
};

int wmain(int argc, const wchar_t **argv)
{
    if (argc > 1)
    {
        ExportArguments args;
        args.FileName = String::FromWString(argv[1]);
        Export(args);
    }
    else
    {
        Application::Init();
        Application::Run(new ModelImporterForm());
        Application::Dispose();
    }
    _CrtDumpMemoryLeaks();
    return 0;
}