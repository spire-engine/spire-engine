#include "LightmapBaker.h"
#include "ObjectSpaceGBufferRenderer.h"
#include "StaticSceneRenderer.h"
#include "ObjectSpaceMapSet.h"
#include "Level.h"
#include "StaticMeshActor.h"
#include "VectorMath.h"
#include "Engine.h"
#include "CameraActor.h"

namespace GameEngine
{
    using namespace CoreLib;

    class LightmapBaker
    {
    public:
        LightmapBakingSettings settings;
    private:
        struct RawMapSet
        {
            RawObjectSpaceMap lightMap, diffuseMap, normalMap, positionMap;
            void Init(int w, int h)
            {
                lightMap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
                diffuseMap.Init(RawObjectSpaceMap::DataType::RGBA8, w, h);
                normalMap.Init(RawObjectSpaceMap::DataType::RGB10_X2_SIGNED, w, h);
                positionMap.Init(RawObjectSpaceMap::DataType::RGBA32F, w, h);
            }
        };
        EnumerableDictionary<Actor*, RawMapSet> maps;
        Level* level = nullptr;
        RefPtr<StaticScene> staticScene;
        void AllocLightmaps(LightmapSet& lmSet)
        {
            // in the future we may support a wider range of actors.
            // for now we only allocate lightmaps for static mesh actors.
            for (auto actor : level->Actors)
            {
                if (auto smActor = actor.Value.As<StaticMeshActor>())
                {
                    auto size = (smActor->Bounds.Max - smActor->Bounds.Min).Length();
                    int resolution = Math::Clamp(1 << Math::Log2Ceil((int)(size * settings.ResolutionScale)), settings.MinResolution, settings.MaxResolution);
                    lmSet.ActorLightmapIds[actor.Value.Ptr()] = maps.Count();
                    maps[smActor.Ptr()] = RawMapSet();
                    maps[smActor.Ptr()]().Init(resolution, resolution);
                }
            }
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
            for (auto actor : level->Actors)
            {
                auto map = maps.TryGetValue(actor.Value.Ptr());
                if (!map) continue;
                int width = map->diffuseMap.Width * SuperSampleFactor;
                int height = map->diffuseMap.Height * SuperSampleFactor;
                RefPtr<Texture2D> texDiffuse = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_8);
                RefPtr<Texture2D> texPosition = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texNormal = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, width, height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texDepth = hwRenderer->CreateTexture2D(TextureUsage::SampledDepthAttachment, width, height, 1, StorageFormat::Depth32);
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
            }
        }

        void RenderDebugView(String fileName, LightmapSet & lightmaps)
        {
            List<RawObjectSpaceMap> diffuseMaps;
            diffuseMaps.SetSize(staticScene->MapIds.Count());
            for (auto mid : staticScene->MapIds)
            {
                diffuseMaps[mid.Value] = maps[mid.Key]().diffuseMap;
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
                return VectorMath::Vec3::Create(0.0f, 0.0f, 0.0f);

            return VectorMath::Vec3::Create(1.0f, 1.0f, 1.0f);
        }

        VectorMath::Vec3 ComputeDirectLighting(VectorMath::Vec3 pos, VectorMath::Vec3 normal)
        {
            VectorMath::Vec3 result;
            result.SetZero();
            for (auto & light : staticScene->lights)
            {
                switch (light.Type)
                {
                case StaticLightType::Directional:
                {
                    auto l = light.Direction;
                    auto nDotL = VectorMath::Vec3::Dot(normal, l);
                    Ray shadowRay;
                    shadowRay.tMax = FLT_MAX;
                    shadowRay.Origin = pos;
                    shadowRay.Dir = l;
                    auto p1 = shadowRay.Origin + shadowRay.Dir * 1000.0f;
                    auto shadowFactor = TraceShadowRay(shadowRay);
                    result += light.Intensity * shadowFactor * Math::Max(0.0f, nDotL);
                    break;
                }
                case StaticLightType::Point:
                case StaticLightType::Spot:
                {
                    auto l = light.Position - pos;
                    auto dist = l.Length();
                    if (dist < light.Radius)
                    {
                        auto invDist = 1.0f / dist;
                        l *= invDist;
                        auto nDotL = VectorMath::Vec3::Dot(normal, l);
                        float actualDecay = 1.0f / Math::Max(1.0f, dist * light.Decay);
                        if (light.Type == StaticLightType::Spot)
                        {
                            float ang = acos(VectorMath::Vec3::Dot(l, light.Direction));
                            actualDecay *= Lerp(1.0, 0.0, Math::Clamp((ang - light.SpotFadingStartAngle) / (light.SpotFadingEndAngle - light.SpotFadingStartAngle), 0.0f, 1.0f));
                        }
                        Ray shadowRay;
                        shadowRay.tMax = dist;
                        shadowRay.Origin = pos;
                        shadowRay.Dir = l;
                        auto shadowFactor = TraceShadowRay(shadowRay);
                        result += light.Intensity * actualDecay * shadowFactor * Math::Max(0.0f, nDotL);
                    }
                    break;
                }
                }
            }
            return result;
        }

        void ComputeLightmaps(LightmapSet& lightmaps)
        {
            VectorMath::Vec3 tangentDirs[] =
            {
                VectorMath::Vec3::Create(1.0f, 0.0f, 0.0f),
                VectorMath::Vec3::Create(-1.0f, 0.0f, 0.0f),
                VectorMath::Vec3::Create(0.0f, 0.0f, 1.0f),
                VectorMath::Vec3::Create(0.0f, 0.0f, -1.0f)
            };

            for (auto & map : maps)
            {
                int imageSize = map.Value.diffuseMap.Width * map.Value.diffuseMap.Height;
                #pragma omp parallel for
                for (int pixelIdx = 0; pixelIdx < imageSize; pixelIdx++)
                {
                    int x = pixelIdx % map.Value.diffuseMap.Width;
                    int y = pixelIdx / map.Value.diffuseMap.Width;
                    VectorMath::Vec4 lighting;
                    lighting.SetZero();
                    auto diffuse = map.Value.diffuseMap.GetPixel(x, y);
                    // compute lighting only for lightmap pixels that represent valid surface regions
                    if (diffuse.x > 1e-6f || diffuse.y > 1e-6f || diffuse.z > 1e-6f)
                    {
                        auto posPixel = map.Value.positionMap.GetPixel(x, y);
                        auto pos = posPixel.xyz();
                        auto bias = posPixel.w * 0.6f;
                        auto normal = map.Value.normalMap.GetPixel(x, y).xyz().Normalize();
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
                                    biasedPos = testRay.Origin + testRay.Dir * inter.T + inter.Normal * settings.ShadowBias;
                                }
                            }
                        }
                        lighting = VectorMath::Vec4::Create(ComputeDirectLighting(biasedPos, normal), 1.0f);
                    }
                    map.Value.lightMap.SetPixel(x, y, lighting);
                }
            }
            // move lightmaps to return value
            lightmaps.Lightmaps.SetSize(maps.Count());
            for (auto mid : staticScene->MapIds)
            {
                lightmaps.Lightmaps[mid.Value] = _Move(maps[mid.Key]().lightMap);
            }
        }
    public:
        void BakeLightmaps(LightmapSet& lightmaps, Level* pLevel)
        {
            level = pLevel;
            AllocLightmaps(lightmaps);
            BakeLightmapGBuffers();
            staticScene = BuildStaticScene(level);
            ComputeLightmaps(lightmaps);
            RenderDebugView("LightmapView.bmp", lightmaps);
        }
    };

    void BakeLightmaps(LightmapSet& lightmaps, const LightmapBakingSettings & settings, Level* pLevel)
    {
        LightmapBaker baker;
        baker.settings = settings;
        baker.settings.ResolutionScale = 0.5f;
        baker.BakeLightmaps(lightmaps, pLevel);
    }
}