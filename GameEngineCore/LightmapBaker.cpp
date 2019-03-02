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
                positionMap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
            }
        };
        EnumerableDictionary<Actor*, RawMapSet> maps;
        Level* level = nullptr;
        RefPtr<StaticScene> staticScene;
        void AllocLightmaps()
        {
            // in the future we may support a wider range of actors.
            // for now we only allocate lightmaps for static mesh actors.
            for (auto actor : level->Actors)
            {
                if (auto smActor = actor.Value.As<StaticMeshActor>())
                {
                    auto size = (smActor->Bounds.Max - smActor->Bounds.Min).Length();
                    int resolution = Math::Clamp(1 << Math::Log2Ceil((int)(size * settings.ResolutionScale)), settings.MinResolution, settings.MaxResolution);
                    maps[smActor.Ptr()] = RawMapSet();
                    maps[smActor.Ptr()]().Init(resolution, resolution);
                }
            }
        }
        void BakeLightmapGBuffers()
        {
            HardwareRenderer* hwRenderer = Engine::Instance()->GetRenderer()->GetHardwareRenderer();
            RefPtr<ObjectSpaceGBufferRenderer> renderer = CreateObjectSpaceGBufferRenderer();
            renderer->Init(Engine::Instance()->GetRenderer()->GetHardwareRenderer(), Engine::Instance()->GetRenderer()->GetRendererService(), "LightmapGBufferGen.slang");
            for (auto actor : level->Actors)
            {
                auto map = maps.TryGetValue(actor.Value.Ptr());
                if (!map) continue;
                RefPtr<Texture2D> texDiffuse = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, map->diffuseMap.Width, map->diffuseMap.Height, 1, StorageFormat::RGBA_8);
                RefPtr<Texture2D> texPosition = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, map->positionMap.Width, map->positionMap.Height, 1, StorageFormat::RGBA_F32);
                RefPtr<Texture2D> texNormal = hwRenderer->CreateTexture2D(TextureUsage::SampledColorAttachment, map->normalMap.Width, map->normalMap.Height, 1, StorageFormat::RGBA_F32);
                Array<Texture2D*, 3> dest;
                Array<StorageFormat, 3> formats;
                dest.SetSize(3);
                dest[0] = texDiffuse.Ptr();
                dest[1] = texPosition.Ptr();
                dest[2] = texNormal.Ptr();
                formats.SetSize(3);
                formats[0] = StorageFormat::RGBA_8;
                formats[1] = StorageFormat::RGBA_F32;
                formats[2] = StorageFormat::RGBA_F32;
                renderer->RenderObjectSpaceMap(dest.GetArrayView(), formats.GetArrayView(), actor.Value.Ptr(), map->diffuseMap.Width, map->diffuseMap.Height);
                texDiffuse->GetData(0, map->diffuseMap.GetBuffer(), map->diffuseMap.Width*map->diffuseMap.Height * sizeof(uint32_t));
                List<float> buffer;
                buffer.SetSize(map->positionMap.Width * map->positionMap.Height * 4);
                texPosition->GetData(0, buffer.Buffer(), map->positionMap.Width*map->positionMap.Height * sizeof(float) * 4);
                float* dst = (float*)map->positionMap.GetBuffer();
                for (int i = 0; i < map->positionMap.Width*map->positionMap.Height; i++)
                {
                    dst[i * 3] = buffer[i * 4];
                    dst[i * 3 + 1] = buffer[i * 4 + 1];
                    dst[i * 3 + 2] = buffer[i * 4 + 2];
                }
                buffer.SetSize(map->normalMap.Width * map->normalMap.Height * 4);
                texNormal->GetData(0, buffer.Buffer(), map->normalMap.Width*map->normalMap.Height * sizeof(float) * 4);
                uint32_t* dstNormal = (uint32_t*)map->normalMap.GetBuffer();
                for (int i = 0; i < map->normalMap.Width*map->normalMap.Height; i++)
                {
                    dstNormal[i] = PackRGB10(buffer[i * 4], buffer[i * 4 + 1], buffer[i * 4 + 2]);
                }
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
                    shadowRay.Origin = pos + normal * settings.ShadowBias + l * settings.ShadowBias;
                    shadowRay.Dir = l;
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
                        shadowRay.Origin = pos + normal * settings.ShadowBias + l * settings.ShadowBias;
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
                    if (x == 140 && y == 587)
                        printf("break");
                    auto diffuse = map.Value.diffuseMap.GetPixel(x, y);
                    // compute lighting only for lightmap pixels that represent valid surface regions
                    if (diffuse.x > 1e-6f || diffuse.y > 1e-6f || diffuse.z > 1e-6f)
                    {
                        auto pos = map.Value.positionMap.GetPixel(x, y).xyz();
                        auto normal = map.Value.normalMap.GetPixel(x, y).xyz().Normalize();
                        lighting = VectorMath::Vec4::Create(ComputeDirectLighting(pos, normal), 1.0f);
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
            AllocLightmaps();
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
        baker.BakeLightmaps(lightmaps, pLevel);
    }
}