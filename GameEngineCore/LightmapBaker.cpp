#include "LightmapBaker.h"
#include "ObjectSpaceGBufferRenderer.h"
#include "StaticSceneRenderer.h"
#include "ObjectSpaceMapSet.h"
#include "Level.h"
#include "StaticMeshActor.h"
#include "CoreLib/VectorMath.h"
#include "Engine.h"
#include "CameraActor.h"
#include "CoreLib/Threading.h"
#include "LightmapUVGeneration.h"
#include <atomic>

namespace GameEngine
{
    using namespace CoreLib;
    static thread_local int pixelCounter = 0;
    static thread_local bool threadCancelled = false;
    static thread_local unsigned int threadRandomSeed = 0;
    class LightmapBakerImpl : public LightmapBaker
    {
    public:
        LightmapBakingSettings settings;
        LightmapSet lightmaps, lightmapsReturn;
        ComputeKernel* lightmapComrpessionKernel;
        int totalBlocks;
        std::atomic<int> completedBlocks;
    private:
        struct RawMapSet
        {
            RawObjectSpaceMap lightMap, indirectLightmap, diffuseMap, normalMap, positionMap, dynamicDirectLighting;
            IntSet validPixels;
            void Init(int w, int h)
            {
                lightMap.Init(RawObjectSpaceMap::DataType::RGBA16F, w, h);
                indirectLightmap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
                diffuseMap.Init(RawObjectSpaceMap::DataType::RGBA8, w, h);
                normalMap.Init(RawObjectSpaceMap::DataType::RGB10_X2_SIGNED, w, h);
                positionMap.Init(RawObjectSpaceMap::DataType::RGBA32F, w, h);
                dynamicDirectLighting.Init(RawObjectSpaceMap::DataType::RGBA16F, w, h);
                validPixels.SetMax(w * h);
            }
        };
        List<RawMapSet> maps;
        Level* level = nullptr;
        RefPtr<StaticScene> staticScene;
        void AllocLightmaps()
        {
            // in the future we may support a wider range of actors.
            // for now we only allocate lightmaps for static mesh actors.
            List<int> mapResolutions;
            for (auto actor : level->Actors)
            {
                if (auto smActor = actor.Value.As<StaticMeshActor>())
                {
                    if (smActor->IncludeInBaking.GetValue())
                    {
                        auto transformMatrix = smActor->LocalTransform.GetValue();
                        float scale = Math::Max(transformMatrix.m[0][0], transformMatrix.m[1][1], transformMatrix.m[2][2]);
                        auto size = sqrt(smActor->GetMesh()->GetSurfaceArea()) * scale;
                        int resolution = Math::Clamp(1 << Math::Log2Ceil((int)(size * settings.ResolutionScale)), settings.MinResolution, settings.MaxResolution);
                        lightmaps.ActorLightmapIds[actor.Value.Ptr()] = mapResolutions.Count();
                        mapResolutions.Add(resolution);
                    }
                }
            }
            maps.SetSize(mapResolutions.Count());
            for (int i = 0; i < maps.Count(); i++)
                maps[i].Init(mapResolutions[i], mapResolutions[i]);
            lightmaps.Lightmaps.SetSize(mapResolutions.Count());
        }
        template<typename TResult, typename TSource, typename TSelectFunc>
        void ReadAndDownSample(Texture2D* texture, TResult* dstBuffer, TSource srcZero, int w, int h, int superSample, const TSelectFunc & select)
        {
            auto dstZero = select(srcZero);
            List<TSource> buffer;
            buffer.SetSize(w * h);
            texture->GetData(0, buffer.Buffer(), w * h * sizeof(TSource));
            for (int y = 0; y < h / superSample; y++)
                for (int x = 0; x < w / superSample; x++)
                {
                    int destId = y * (h / superSample) + x;
                    dstBuffer[destId] = dstZero;
                    for (int xx = 0; xx < superSample; xx++)
                    {
                        for (int yy = 0; yy < superSample; yy++)
                        {
                            auto src = buffer[(y + yy) * w + x + xx];
                            if (src != srcZero)
                            {
                                dstBuffer[destId] = select(src);
                                goto procEnd;
                            }
                        }
                    }
                procEnd:;
                }

        }
        void BakeLightmapGBuffers()
        {
            const int SuperSampleFactor = 1;
            HardwareRenderer* hwRenderer = Engine::Instance()->GetRenderer()->GetHardwareRenderer();
            RefPtr<ObjectSpaceGBufferRenderer> renderer = CreateObjectSpaceGBufferRenderer();
            renderer->Init(Engine::Instance()->GetRenderer()->GetHardwareRenderer(), Engine::Instance()->GetRenderer()->GetRendererService(), "LightmapGBufferGen.slang");
            int progress = 0;
            StatusChanged("Baking G-Buffers...");
            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            for (auto actor : level->Actors)
            {
                auto mapId = lightmaps.ActorLightmapIds.TryGetValue(actor.Value.Ptr());
                if (!mapId) continue;
                auto map = maps.Buffer() + *mapId;
                int width = map->diffuseMap.Width * SuperSampleFactor;
                int height = map->diffuseMap.Height * SuperSampleFactor;
                RefPtr<Texture2D> texDiffuse = hwRenderer->CreateTexture2D("LightmapBaker::texDiffuse", TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_8);
                RefPtr<Texture2D> texPosition = hwRenderer->CreateTexture2D("LightmapBaker::texPosition", TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texNormal = hwRenderer->CreateTexture2D("LightmapBaker::texNormal", TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texDepth = hwRenderer->CreateTexture2D("LightmapBaker::texDepth", TextureUsage::SampledDepthAttachment, width, height, 1, StorageFormat::Depth32);
                Array<Texture2D*, 4> dest;
                Array<StorageFormat, 4> formats;
                dest.SetSize(4);
                dest[0] = texDiffuse.Ptr();
                dest[1] = texPosition.Ptr();
                dest[2] = texNormal.Ptr();
                dest[3] = texDepth.Ptr();
                formats.SetSize(4);
                formats[0] = StorageFormat::RGBA_8;
                formats[1] = StorageFormat::RGBA_F32;
                formats[2] = StorageFormat::RGBA_F32;
                formats[3] = StorageFormat::Depth32;
                renderer->RenderObjectSpaceMap(dest.GetArrayView(), formats.GetArrayView(), actor.Value.Ptr(), map->diffuseMap.Width, map->diffuseMap.Height);

                ReadAndDownSample(texDiffuse.Ptr(), (uint32_t*)map->diffuseMap.GetBuffer(), (uint32_t)0, width, height, SuperSampleFactor, 
                    [](uint32_t x) {return x; });
                ReadAndDownSample(texPosition.Ptr(), (VectorMath::Vec4*)map->positionMap.GetBuffer(), VectorMath::Vec4::Create(0.0f), width, height, SuperSampleFactor, 
                    [](VectorMath::Vec4 x) {return x; });
                ReadAndDownSample(texNormal.Ptr(), (uint32_t*)map->normalMap.GetBuffer(), VectorMath::Vec4::Create(0.0f), width, height, SuperSampleFactor,
                    [](VectorMath::Vec4 x) {return PackRGB10(x.x, x.y, x.z); });
                for (int i = 0; i < height; i++)
                    for (int j = 0; j < width; j++)
                    {
                        auto diffusePixel = map->diffuseMap.GetPixel(j, i);
                        if (diffusePixel.x > 1e-5f || diffusePixel.y > 1e-5f || diffusePixel.z > 1e-5f)
                            map->validPixels.Add(i * width + j);
                    }
                //map->diffuseMap.DebugSaveAsImage("d:/debugDiffuse.pfm");
                progress++;
                ProgressChanged(LightmapBakerProgressChangedEventArgs(progress, lightmaps.ActorLightmapIds.Count()));
                if (isCancelled) return;
            }
        }

        void RenderDebugView(String fileName)
        {
            List<RawObjectSpaceMap*> diffuseMaps;
            diffuseMaps.SetSize(maps.Count());
            for (int i = 0; i < diffuseMaps.Count(); i++)
            {
                diffuseMaps[i] = &maps[i].diffuseMap;
            }
            RefPtr<StaticSceneRenderer> staticRenderer = CreateStaticSceneRenderer();
            staticRenderer->SetCamera(level->CurrentCamera->GetCameraTransform(), level->CurrentCamera->FOV, 1024, 1024);
            auto & image = staticRenderer->Render(staticScene.Ptr(), diffuseMaps, lightmaps);
            image.GetImageRef().SaveAsBmpFile(fileName);
        }

        float Lerp(float a, float b, float t)
        {
            return a * (1.0f - t) + b * t;
        }

        VectorMath::Vec3 TraceShadowRay(Ray & shadowRay)
        {
            auto inter = staticScene->TraceRay(shadowRay);
            if (inter.IsHit)
            {
                if (inter.CastShadow)
                {
                    auto diffuse = maps[inter.MapId].diffuseMap.Sample(inter.UV);
                    if (diffuse.w < 1.0f)
                    {
                        Ray newShadowRay;
                        newShadowRay.Origin = shadowRay.Origin + shadowRay.Dir * (inter.T + settings.ShadowBias);
                        newShadowRay.tMax = shadowRay.tMax - (inter.T + settings.ShadowBias);
                        newShadowRay.Dir = shadowRay.Dir;
                        auto rs = TraceShadowRay(newShadowRay);
                        return diffuse.xyz() * rs * (1.0f - diffuse.w);
                    }
                    else
                        return VectorMath::Vec3::Create(0.0f, 0.0f, 0.0f);
                }
                else
                {
                    Ray newShadowRay;
                    newShadowRay.Origin = shadowRay.Origin + shadowRay.Dir * (inter.T + settings.ShadowBias);
                    newShadowRay.tMax = shadowRay.tMax - (inter.T + settings.ShadowBias);
                    newShadowRay.Dir = shadowRay.Dir;
                    return TraceShadowRay(newShadowRay);
                }
                
            }

            return VectorMath::Vec3::Create(1.0f, 1.0f, 1.0f);
        }

        VectorMath::Vec3 ComputeDirectLighting(VectorMath::Vec3 pos, VectorMath::Vec3 normal, VectorMath::Vec3 & dynamicDirectLighting)
        {
            VectorMath::Vec3 result;
            result.SetZero();
            dynamicDirectLighting.SetZero();
            for (auto & light : staticScene->lights)
            {
                VectorMath::Vec3 lighting;
                auto l = light.Position - pos;
                auto dist = l.Length();
                float actualDecay = 1.0f;
                if (light.Radius != 0.0f)
                {
                    actualDecay = Lerp(1.0, 0.0f, sqrt(dist / light.Radius));
                    if (dist > light.Radius)
                        continue;
                }
                switch (light.Type)
                {
                case StaticLightType::Directional:
                {
                    l = light.Direction;
                    auto nDotL = VectorMath::Vec3::Dot(normal, l);
                    Ray shadowRay;
                    shadowRay.tMax = FLT_MAX;
                    shadowRay.Origin = pos;
                    shadowRay.Dir = l;
                    auto p1 = shadowRay.Origin + shadowRay.Dir * 1000.0f;
                    auto shadowFactor = light.EnableShadows ? TraceShadowRay(shadowRay) : VectorMath::Vec3::Create(1.0f);
                    shadowFactor *= actualDecay;
                    lighting = light.Intensity * shadowFactor * Math::Max(0.0f, nDotL);
                    break;
                }
                case StaticLightType::Point:
                case StaticLightType::Spot:
                {
                    if (dist < light.Radius || light.Radius == 0.0f)
                    {
                        auto invDist = 1.0f / dist;
                        l *= invDist;
                        auto nDotL = VectorMath::Vec3::Dot(normal, l);
                        
                        if (light.Type == StaticLightType::Spot)
                        {
                            float ang = acos(VectorMath::Vec3::Dot(l, light.Direction));
                            actualDecay *= Lerp(1.0, 0.0, Math::Clamp((ang - light.SpotFadingStartAngle) / (light.SpotFadingEndAngle - light.SpotFadingStartAngle), 0.0f, 1.0f));
                        }
                        Ray shadowRay;
                        shadowRay.tMax = dist;
                        shadowRay.Origin = pos;
                        shadowRay.Dir = l;
                        auto shadowFactor = light.EnableShadows ? TraceShadowRay(shadowRay) : VectorMath::Vec3::Create(1.0f);
                        lighting = light.Intensity * actualDecay * shadowFactor * Math::Max(0.0f, nDotL);
                    }
                    break;
                }
                }
                result += lighting;
                if (!light.IncludeDirectLighting)
                    dynamicDirectLighting += lighting;
            }
            return result;
        }

        VectorMath::Vec3 UniformSampleHemisphere(float r1, float r2)
        {
            float sinTheta = sqrtf(1 - r1 * r1);
            float phi = 2.0f * Math::Pi * r2;
            float x = sinTheta * cosf(phi);
            float z = sinTheta * sinf(phi);
            return VectorMath::Vec3::Create(x, r1, z);
        }

        VectorMath::Vec3 TraceSampleRay(Ray& ray, float minValidDist, bool& isInvalid, int recurseLevel = 0)
        {
            auto inter = staticScene->TraceRay(ray);
            if (inter.IsHit)
            {
                auto surfaceAlbedo = maps[inter.MapId].diffuseMap.Sample(inter.UV);
                if (surfaceAlbedo.w == 1.0f)
                {
                    if (VectorMath::Vec3::Dot(inter.Normal, ray.Dir) < 0.0f)
                    {
                        auto directDiffuse = maps[inter.MapId].lightMap.Sample(inter.UV).xyz();
                        auto indirectDiffuse = maps[inter.MapId].indirectLightmap.Sample(inter.UV).xyz();
                        auto totalDiffuseLight = directDiffuse + indirectDiffuse;
                        return totalDiffuseLight * surfaceAlbedo.xyz();
                    }
                    else
                    {
                        if (inter.T < minValidDist)
                            isInvalid = true;
                        return VectorMath::Vec3::Create(0.0f, 0.0f, 0.0f);
                    }
                }
                else
                {
                    if (recurseLevel > settings.SampleCount)
                    {
                        return VectorMath::Vec3::Create(0.0f, 0.0f, 0.0f);
                    }
                    Ray newRay = ray;
                    newRay.Origin = ray.Origin + ray.Dir * (inter.T + settings.ShadowBias);
                    newRay.Dir = ray.Dir;
                    newRay.tMax = ray.tMax - (inter.T + settings.ShadowBias);
                    bool isInvalidInner = false;
                    auto result = TraceSampleRay(newRay, minValidDist, isInvalidInner, recurseLevel + 1);
                    return result * surfaceAlbedo.xyz() * surfaceAlbedo.w;
                }
            }
            else
            {
                if (ray.Dir.y > 0.0f)
                    return staticScene->ambientColor;
                return VectorMath::Vec3::Create(0.0f);
            }
        }

        VectorMath::Vec3 ComputeIndirectLighting(Random & random, VectorMath::Vec3 pos, VectorMath::Vec3 normal, int sampleCount, float minValidDistance, bool & isInvalidRegion)
        {
            VectorMath::Vec3 result;
            result.SetZero();
            static const float invPi = 1.0f / Math::Pi;
            VectorMath::Vec3 tangent;
            VectorMath::GetOrthoVec(tangent, normal);
            auto binormal = VectorMath::Vec3::Cross(tangent, normal);
            for (int i = 0; i < sampleCount; i++)
            {
                float r1 = random.NextFloat();
                float r2 = random.NextFloat();
                Ray ray;
                ray.Origin = pos;
                auto tanDir = UniformSampleHemisphere(r1, r2);
                ray.Dir = tangent * tanDir.x + normal * tanDir.y + binormal * tanDir.z;
                ray.tMax = FLT_MAX;
                auto sampleColor = TraceSampleRay(ray, minValidDistance, isInvalidRegion) * r1;
                result += sampleColor;
            }
            result *= 2.0f / (float)sampleCount;
            return result;
        }

        void BiasGBufferPositions()
        {
            VectorMath::Vec3 tangentDirs[] =
            {
                VectorMath::Vec3::Create(1.0f, 0.0f, 0.0f),
                VectorMath::Vec3::Create(-1.0f, 0.0f, 0.0f),
                VectorMath::Vec3::Create(0.0f, 0.0f, 1.0f),
                VectorMath::Vec3::Create(0.0f, 0.0f, -1.0f)
            };
            int completedMaps = 0;
            for (auto & map : maps)
            {
                int imageSize = map.diffuseMap.Width * map.diffuseMap.Height;
                if (isCancelled) return;
                #pragma omp parallel for
                for (int pixelIdx = 0; pixelIdx < imageSize; pixelIdx++)
                {
                    int x = pixelIdx % map.diffuseMap.Width;
                    int y = pixelIdx / map.diffuseMap.Width;
                    auto diffuse = map.diffuseMap.GetPixel(x, y);
                    // compute lighting only for lightmap pixels that represent valid surface regions
                    if (diffuse.x > 1e-6f || diffuse.y > 1e-6f || diffuse.z > 1e-6f)
                    {
                        auto posPixel = map.positionMap.GetPixel(x, y);
                        auto pos = posPixel.xyz();
                        auto bias = posPixel.w * 0.8f;
                        auto normal = map.normalMap.GetPixel(x, y).xyz().Normalize();
                        // shoot random rays and find if the ray hits the back of some nearby face,
                        // if so, shift pos to the front of that face to avoid shadow leaking
                        VectorMath::Vec3 tangent, binormal;
                        VectorMath::GetOrthoVec(tangent, normal);
                        binormal = VectorMath::Vec3::Cross(normal, tangent);
                        auto biasedPos = pos + normal * settings.ShadowBias;
                        float minT = FLT_MAX;
                        for (auto tangentDir : tangentDirs)
                        {
                            auto testDir = tangent * tangentDir.x + normal * tangentDir.y + binormal * tangentDir.z;
                            Ray testRay;
                            testRay.Origin = biasedPos;
                            testRay.Dir = testDir;
                            testRay.tMax = bias;
                            auto inter = staticScene->TraceRay(testRay);
                            if (inter.IsHit && VectorMath::Vec3::Dot(inter.Normal, testDir) > 0.0f)
                            {
                                if (inter.T < minT)
                                {
                                    minT = inter.T;
                                    biasedPos = testRay.Origin + testRay.Dir * inter.T + inter.Normal * (bias);
                                }
                            }
                        }
                        map.positionMap.SetPixel(x, y, VectorMath::Vec4::Create(biasedPos, posPixel.w));
                    }
                }

                completedMaps++;
                ProgressChanged(LightmapBakerProgressChangedEventArgs(completedMaps, maps.Count()));
            }
        }

        void ComputeLightmaps_Direct()
        {
            int completedMaps = 0;
            for (auto & map : maps)
            {
                if (isCancelled) return;
                int imageSize = map.diffuseMap.Width * map.diffuseMap.Height;
                #pragma omp parallel for
                for (int pixelIdx = 0; pixelIdx < imageSize; pixelIdx++)
                {
                    if (map.validPixels.Contains(pixelIdx))
                    {
                        pixelCounter++;
                        if (threadCancelled || (pixelCounter & 15) == 0)
                        {
                            threadCancelled = isCancelled;
                        }
                        if (threadCancelled) continue;
                        int x = pixelIdx % map.diffuseMap.Width;
                        int y = pixelIdx / map.diffuseMap.Width;
                        
                        VectorMath::Vec4 lighting;
                        lighting.SetZero();
                        VectorMath::Vec3 dynamicDirectLighting;
                        auto diffuse = map.diffuseMap.GetPixel(x, y);
                        auto posPixel = map.positionMap.GetPixel(x, y);
                        auto pos = posPixel.xyz();
                        auto normal = map.normalMap.GetPixel(x, y).xyz().Normalize();
                        
                        lighting = VectorMath::Vec4::Create(ComputeDirectLighting(pos, normal, dynamicDirectLighting), 1.0f);
                        map.lightMap.SetPixel(x, y, lighting);
                        map.dynamicDirectLighting.SetPixel(x, y, VectorMath::Vec4::Create(dynamicDirectLighting, 1.0f));
                    }
                }
                completedMaps++;
                ProgressChanged(LightmapBakerProgressChangedEventArgs(completedMaps, maps.Count()));
            }
        }
        static const int MaxLightmapBlockSize = 16;
        void ComputeIndirectLightmapBlock(RawMapSet& map, RawObjectSpaceMap& resultMap, int sampleCount, int x0, int y0, int blockSize,
            VectorMath::Vec4* result, VectorMath::Vec3* normals, VectorMath::Vec3* positions, bool* valid, bool * computed)
        {
            int x1 = x0 + blockSize - 1;
            int y1 = y0 + blockSize - 1;
            for (int i = 0; i < 4; i++)
            {
                if (computed[i]) continue;
                valid[i] = false;
                int x = (i & 1) ? x0 : x1;
                int y = (i & 2) ? y0 : y1;
                int pixelIdx = y * map.diffuseMap.Width + x;
                if (map.validPixels.Contains(pixelIdx))
                {
                    valid[i] = true;
                    pixelCounter++;
                    VectorMath::Vec4 lighting;
                    lighting.SetZero();
                    auto posPixel = map.positionMap.GetPixel(x, y);
                    auto pos = posPixel.xyz();
                    auto normal = map.normalMap.GetPixel(x, y).xyz().Normalize();
                    Random threadRandom(threadRandomSeed);
                    bool isInvalidRegion = false;
                    positions[i] = pos;
                    normals[i] = normal;
                    lighting = VectorMath::Vec4::Create(ComputeIndirectLighting(threadRandom, pos, normal, sampleCount, posPixel.w*2.0f, isInvalidRegion), 1.0f);
                    if (sampleCount >= settings.SampleCount && isInvalidRegion)
                    {
                        map.validPixels.Remove(pixelIdx);
                        valid[i] = false;
                    }
                    resultMap.SetPixel(x, y, lighting);
                    result[i] = lighting;
                    threadRandomSeed = threadRandom.GetSeed();
                }
            }
            if (blockSize > 2)
            {
                // do we need to refine?
                bool shouldRefine = (!valid[0] || !valid[1] || !valid[2] || !valid[3]);
                shouldRefine = shouldRefine || (positions[3] - positions[0]).Length() > settings.IndirectLightingWorldGranularity;
                if (sampleCount == settings.FinalGatherSampleCount)
                {
                    shouldRefine = shouldRefine || (result[1] - result[0]).Length() > 0.1f || (result[2] - result[0]).Length() > 0.1f ||
                        (result[3] - result[0]).Length() > settings.FinalGatherAdaptiveSampleThreshold;
                }
                
                if (shouldRefine)
                {
                    VectorMath::Vec4 nResult[4];
                    VectorMath::Vec3 nPositions[4];
                    VectorMath::Vec3 nNormals[4];
                    bool nValid[4], nComputed[4];
                    for (int j = 0; j < 4; j++)
                    {
                        nValid[j] = valid[j];
                        nResult[j] = result[j];
                        nPositions[j] = positions[j];
                        nNormals[j] = normals[j];
                        nComputed[0] = nComputed[1] = nComputed[2] = nComputed[3] = false;
                        nComputed[j] = true; 
                        int nx0 = x0 + ((j & 1) ? (blockSize >> 1) : 0);
                        int ny0 = y0 + ((j & 2) ? (blockSize >> 1) : 0);
                        ComputeIndirectLightmapBlock(map, resultMap, sampleCount, nx0, ny0, blockSize >> 1, nResult, nNormals, nPositions, nValid, nComputed);
                    }
                }
                else
                {
                    float invBlockSize = 1.0f / (blockSize - 1);
                    VectorMath::Vec4 first[MaxLightmapBlockSize];
                    VectorMath::Vec4 last[MaxLightmapBlockSize];
                    first[0] = result[0];
                    first[blockSize - 1] = result[1];
                    last[0] = result[2];
                    last[blockSize - 1] = result[3];
                    // fill first row and last row
                    for (int j = 1; j < blockSize - 1; j++)
                    {
                        float t = j * invBlockSize;
                        float invT = 1.0f - t;
                        first[j] = result[0] * invT + result[1] * t;
                        resultMap.SetPixel(x0 + j, y0, first[j]);
                        last[j] = result[2] * invT + result[3] * t;
                        resultMap.SetPixel(x0 + j, y1, last[j]);
                    }
                    // fill all columns
                    for (int j = 0; j < blockSize; j++)
                    {
                        auto a = first[j];
                        auto b = last[j];
                        for (int k = 1; k < blockSize - 1; k++)
                        {
                            float t = k * invBlockSize;
                            float invT = 1.0f - t;
                            resultMap.SetPixel(x0 + j, y0 + k, a*invT + b * t);
                        }
                    }
                }
            }
        }

        void ComputeLightmaps_Indirect(int sampleCount)
        {
            static int iteration = 0;
            iteration++;
            totalBlocks = 0;
            for (auto & map : maps)
            {
                int horizontalBlockCount = (map.diffuseMap.Width + MaxLightmapBlockSize - 1) / MaxLightmapBlockSize;
                int blockCount = horizontalBlockCount * (map.diffuseMap.Height + MaxLightmapBlockSize - 1) / MaxLightmapBlockSize;
                totalBlocks += blockCount;
            }
            completedBlocks = 0;
            for (auto & map : maps)
            {
                if (isCancelled) return;
                int horizontalBlockCount = (map.diffuseMap.Width + MaxLightmapBlockSize - 1) / MaxLightmapBlockSize;
                int blockCount = horizontalBlockCount * (map.diffuseMap.Height + MaxLightmapBlockSize - 1) / MaxLightmapBlockSize;
                auto resultMap = map.indirectLightmap;
                #pragma omp parallel for
                for (int blockIdx = 0; blockIdx < blockCount; blockIdx++)
                {
                    if (threadCancelled || (pixelCounter & 15) == 0)
                    {
                        threadCancelled = isCancelled;
                    }
                    if (threadCancelled) continue;

                    int x0 = (blockIdx % horizontalBlockCount) * MaxLightmapBlockSize;
                    int y0 = (blockIdx / horizontalBlockCount) * MaxLightmapBlockSize;
                    VectorMath::Vec3 positions[4], normals[4];
                    VectorMath::Vec4 results[4];
                    bool valid[4];
                    bool computed[4] = { false, false ,false, false };
                    ComputeIndirectLightmapBlock(map, resultMap, sampleCount, x0, y0, MaxLightmapBlockSize,
                        results, normals, positions, valid, computed);
                    auto progress = completedBlocks.fetch_add(1);
                    if ((progress & 7) == 0)
                        ProgressChanged(LightmapBakerProgressChangedEventArgs(progress + 1, totalBlocks));
                }
                map.indirectLightmap = _Move(resultMap);
            }
        }

        RawObjectSpaceMap blurTempMap;
        Dictionary<int, List<float>> blurKernels;
        ArrayView<float> GetBlurKernel(int radius)
        {
            int size = radius + 1;
            if (auto rs = blurKernels.TryGetValue(size))
                return rs->GetArrayView();
            List<float> kernel;
            kernel.SetSize(size);
            float sigma = 0.6f;
            float invTwoSigmaSquare = 1.0f / (2.0f * sigma * sigma);
            for (int i = 0; i < size; i++)
            {
                float dist = i / (float)radius;
                float weight = exp(-dist * dist * invTwoSigmaSquare);
                kernel[i] = weight;
            }
            blurKernels[size] = kernel;
            return blurKernels[size]().GetArrayView();
        }
        void BlurLightmap(IntSet& validPixels, int blurRadius, RawObjectSpaceMap & lightmap)
        {
            blurTempMap.Init(lightmap.GetDataType(), lightmap.Width, lightmap.Height);
            ArrayView<float> kernel = GetBlurKernel(blurRadius);
            #pragma omp parallel for
            for (int y = 0; y < lightmap.Height; y++)
            {
                for (int x = 0; x < lightmap.Width; x++)
                {
                    if (validPixels.Contains(y*lightmap.Width + x))
                    {
                        VectorMath::Vec3 value;
                        float sumWeight = kernel[0];
                        value = lightmap.GetPixel(x, y).xyz();
                        for (int ix = x - 1; ix >= Math::Max(0, x - blurRadius); ix--)
                        {
                            if (validPixels.Contains(y*lightmap.Width + ix))
                            {
                                auto otherVal = lightmap.GetPixel(ix, y).xyz();
                                value += otherVal * kernel[x - ix];
                                sumWeight += kernel[x - ix];
                            }
                            else
                                break;
                        }
                        for (int ix = x + 1; ix <= Math::Min(lightmap.Width - 1, x + blurRadius); ix++)
                        {
                            if (validPixels.Contains(y*lightmap.Width + ix))
                            {
                                auto otherVal = lightmap.GetPixel(ix, y).xyz();
                                value += otherVal * kernel[ix - x];
                                sumWeight += kernel[ix - x];
                            }
                            else
                                break;
                        }
                        value *= 1.0f / sumWeight;
                        blurTempMap.SetPixel(x, y, VectorMath::Vec4::Create(value, 1.0f));
                    }
                }
            }

            #pragma omp parallel for
            for (int y = 0; y < lightmap.Height; y++)
            {
                for (int x = 0; x < lightmap.Width; x++)
                {
                    if (validPixels.Contains(y*lightmap.Width + x))
                    {
                        VectorMath::Vec3 value;
                        value = blurTempMap.GetPixel(x, y).xyz();
                        float sumWeight = kernel[0];
                        for (int iy = y - 1; iy >= Math::Max(0, y - blurRadius); iy--)
                        {
                            if (validPixels.Contains(iy*lightmap.Width + x))
                            {
                                auto otherVal = blurTempMap.GetPixel(x, iy).xyz();
                                value += otherVal * kernel[y - iy];
                                sumWeight += kernel[y - iy];
                            }
                            else
                                break;
                        }
                        for (int iy = y + 1; iy <= Math::Min(lightmap.Height - 1, y + blurRadius); iy++)
                        {
                            if (validPixels.Contains(iy*lightmap.Width + x))
                            {
                                auto otherVal = blurTempMap.GetPixel(x, iy).xyz();
                                value += otherVal * kernel[iy - y];
                                sumWeight += kernel[iy - y];
                            }
                            else
                                break;
                        }
                        value *= 1.0f / sumWeight;
                        lightmap.SetPixel(x, y, VectorMath::Vec4::Create(value, 1.0f));
                    }
                }
            }
        }

        void CompositeLightmaps()
        {
            lightmaps.Lightmaps.SetSize(maps.Count());
            for (int i = 0; i < maps.Count(); i++)
            {
                auto & lm = lightmaps.Lightmaps[i];
                lm.Init(RawObjectSpaceMap::DataType::RGB32F, maps[i].lightMap.Width, maps[i].lightMap.Height);
                int indirectBlurRadius = Math::Clamp(maps[i].indirectLightmap.Width / 50, 1, 40);

                // get direct lighting
                #pragma omp parallel for
                for (int y = 0; y < lm.Height; y++)
                    for (int x = 0; x < lm.Width; x++)
                        lm.SetPixel(x, y, maps[i].lightMap.GetPixel(x, y) - maps[i].dynamicDirectLighting.GetPixel(x, y));

                // blur direct lighting in final lightmap
                BlurLightmap(maps[i].validPixels, 2, lm);
                // composite indirect lighting
                BlurLightmap(maps[i].validPixels, indirectBlurRadius, maps[i].indirectLightmap);
                #pragma omp parallel for
                for (int y = 0; y < lm.Height; y++)
                    for (int x = 0; x < lm.Width; x++)
                        lm.SetPixel(x, y, lm.GetPixel(x, y) + maps[i].indirectLightmap.GetPixel(x, y));

                // dilate
                #pragma omp parallel for
                for (int y = 0; y < lm.Height; y++)
                    for (int x = 0; x < lm.Width; x++)
                    {
                        if (!maps[i].validPixels.Contains(y * lm.Width + x) 
                            || maps[i].indirectLightmap.GetPixel(x, y).xyz().Length2() < 1e-6f
                            )
                        {
                            for (int iy = Math::Max(y-1, 0); iy <= Math::Min(y + 1, lm.Height-1); iy++)
                                for (int ix = Math::Max(x - 1, 0); ix <= Math::Min(x + 1, lm.Width - 1); ix++)
                                {
                                    if (maps[i].validPixels.Contains(iy * lm.Width + ix) 
                                        && maps[i].indirectLightmap.GetPixel(ix, iy).xyz().Length2() > 1e-4f
                                        )
                                    {
                                        lm.SetPixel(x, y, lm.GetPixel(ix, iy));
                                        goto outContinue;
                                    }
                                }
                        outContinue:;
                        }
                    }
                
            }
        }
        void CheckUVs()
        {
            CoreLib::EnumerableDictionary<String, Mesh*> referencedMeshes;
            for (auto & actor : level->Actors)
            {
                if (auto staticMeshActor = dynamic_cast<StaticMeshActor*>(actor.Value.Ptr()))
                {
                    if (staticMeshActor->GetMesh())
                    {
                        referencedMeshes.AddIfNotExists(staticMeshActor->GetMesh()->GetFileName(), staticMeshActor->GetMesh());
                    }
                }
            }
            auto list = From(referencedMeshes).ToList();
            #pragma omp parallel for
            for (int i = 0; i < list.Count(); i++)
            {
                if (isCancelled)
                    continue;
                auto kv = list[i];
                if (kv.Value->GetMinimumLightmapResolution() <= 0)
                {
                    StatusChanged(String("Generating unique uv for '") + kv.Key + "'...");
                    Mesh meshOut;
                    GenerateLightmapUV(&meshOut, kv.Value, settings.MaxResolution, 1 + Math::Log2Ceil(settings.MaxResolution) - Math::Log2Ceil(settings.MinResolution));
                    meshOut.SetMinimumLightmapResolution(settings.MinResolution);
                    auto fullFileName = Engine::Instance()->FindFile(kv.Key, ResourceType::Mesh);
                    try
                    {
                        if (fullFileName.Length())
                            meshOut.SaveToFile(fullFileName);
                    }
                    catch (const CoreLib::IO::IOException&)
                    {
                        StatusChanged(String("Failed to save mesh to '") + fullFileName + "'.");
                    }
                    *kv.Value = _Move(meshOut);
                    MeshChanged(kv.Value);
                }
            }
        }
        
        void CompressLightmaps()
        {
            auto computeTaskManager = Engine::GetComputeTaskManager();
            auto hw = Engine::Instance()->GetRenderer()->GetHardwareRenderer();
            RefPtr<Fence> fence = hw->CreateFence();
            fence->Reset();
            for (auto & lm : lightmaps.Lightmaps)
            {
                int inputBufferSize = (int)lm.Width * lm.Height * sizeof(float) * 3;
                int outputBufferSize = lm.Width * lm.Height;
                RefPtr<Buffer> inputBuffer = hw->CreateBuffer(BufferUsage::StorageBuffer, inputBufferSize);
                RefPtr<Buffer> outputBuffer = hw->CreateBuffer(BufferUsage::StorageBuffer, outputBufferSize);
                int uniformData[2] = { lm.Width >> 2, lm.Height >> 2 };
                Array<ResourceBinding, 2> resBindings;
                resBindings.Add(ResourceBinding(inputBuffer.Ptr(), 0, inputBufferSize));
                resBindings.Add(ResourceBinding(outputBuffer.Ptr(), 0, outputBufferSize));
                inputBuffer->SetData(lm.GetBuffer(), lm.Width*lm.Height * sizeof(float) * 3);
                auto instance = computeTaskManager->CreateComputeTaskInstance(lightmapComrpessionKernel, sizeof(uniformData), false);
                instance->SetBinding(resBindings.GetArrayView());
                instance->SetUniformData(&uniformData, sizeof(uniformData));
                instance->Run(lm.Width >> 2, lm.Height >> 2, 1, fence.Ptr());
                fence->Wait();
                fence->Reset();
                lm.Init(RawObjectSpaceMap::DataType::BC6H, lm.Width, lm.Height);
                outputBuffer->GetData(lm.GetBuffer(), 0, lm.Width * lm.Height);
                if (isCancelled)
                    return;
            }
        }
    public:
        std::atomic<bool> isCancelled;
        std::atomic<bool> started;
        CoreLib::Threading::Thread computeThread;
        void StatusChanged(String status)
        {
            Engine::Instance()->GetMainWindow()->InvokeAsync([=]()
            {
                OnStatusChanged(status);
            });
        }
        void MeshChanged(Mesh* mesh)
        {
            Engine::Instance()->GetMainWindow()->InvokeAsync([=]()
            {
                OnMeshChanged(mesh);
            });
        }
        void ProgressChanged(LightmapBakerProgressChangedEventArgs e)
        {
            Engine::Instance()->GetMainWindow()->InvokeAsync([=]()
            {
                OnProgressChanged(e);
            });
        }
        void Completed()
        {
            bool cancelled = isCancelled;
            lightmapsReturn = lightmaps;
            Engine::Instance()->GetMainWindow()->InvokeAsync([this, cancelled]()
            {
                OnCompleted(cancelled);
            });
        }
        void IterationCompleted()
        {
            if (!isCancelled)
            {
                lightmapsReturn = lightmaps;
                Engine::Instance()->GetMainWindow()->InvokeAsync([=]()
                {
                    OnIterationCompleted();
                });
            }
        }
        void ComputeThreadMain()
        {
            HardwareRenderer* hwRenderer = Engine::Instance()->GetRenderer()->GetHardwareRenderer();
            hwRenderer->ThreadInit(1);

            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            StatusChanged("Checking UVs...");
            CheckUVs();
            if (isCancelled) goto computeThreadEnd;

            AllocLightmaps();
            if (isCancelled) goto computeThreadEnd;
            #pragma omp parallel sections
            {
                #pragma omp section
                {
                    StatusChanged("Building BVH...");
                    ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));

                    staticScene = BuildStaticScene(level);
                }
                #pragma omp section
                {
                    Engine::Instance()->GetRenderer()->GetHardwareRenderer()->ThreadInit(1);
                    StatusChanged("Initializing lightmaps...");
                    BakeLightmapGBuffers();                    
                }
            }
            
            if (isCancelled) goto computeThreadEnd;

            StatusChanged("Refining G-Buffer...");
            BiasGBufferPositions();

            if (isCancelled) goto computeThreadEnd;

            StatusChanged("Computing direct lighting...");
            ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            ComputeLightmaps_Direct();
            CompositeLightmaps();
            if (isCancelled) goto computeThreadEnd;
            IterationCompleted();

            for (int i = 0; i < settings.IndirectLightingBounces; i++)
            {
                StringBuilder statusTextSB;
                statusTextSB << "Computing indirect lighting, pass " << i + 1 << "/" << settings.IndirectLightingBounces;
                if (i == settings.IndirectLightingBounces - 1)
                    statusTextSB << "(final gather)";
                statusTextSB << "...";
                auto statusText = statusTextSB.ToString();
                StatusChanged(statusText);
                ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
                ComputeLightmaps_Indirect(i == settings.IndirectLightingBounces-1 ? settings.FinalGatherSampleCount : Math::Min((i + 1) * 2, settings.SampleCount));
                CompositeLightmaps();
                if (isCancelled) goto computeThreadEnd;
                IterationCompleted();
            }

            StatusChanged("Compressing lightmaps...");
            CompressLightmaps();
        computeThreadEnd:;
            if (!isCancelled)
            {
                StatusChanged("Baking completed.");
                ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            }
            else
            {
                StatusChanged("Baking cancelled.");
                ProgressChanged(LightmapBakerProgressChangedEventArgs(0, 100));
            }
            Completed();
            started = false;
        }
        virtual void Start(const LightmapBakingSettings & pSettings, Level* pLevel) override
        {
            settings = pSettings;
            level = pLevel;
            lightmaps = LightmapSet();
            maps.Clear();
            started = true;
            isCancelled = false;

            computeThread.Start(new CoreLib::Threading::ThreadProc([this]()
            {
                ComputeThreadMain();
            }));
        }
        virtual bool IsRunning() override
        {
            return started;
        }
        virtual bool IsCancelled() override
        {
            return isCancelled;
        }
        virtual LightmapSet& GetLightmapSet() override
        {
            return lightmapsReturn;
        }
        virtual void Wait() override
        {
            computeThread.Join();
        }
        virtual void Cancel() override
        {
            isCancelled = true;
            Wait();
        }
        LightmapBakerImpl()
        {
            auto computeTaskManager = Engine::GetComputeTaskManager();
            lightmapComrpessionKernel = computeTaskManager->LoadKernel("BC6Compression.slang", "cs_main");
            isCancelled = false;
            started = false;
        }
    };
    LightmapBaker * CreateLightmapBaker()
    {
        return new LightmapBakerImpl();
    }
}