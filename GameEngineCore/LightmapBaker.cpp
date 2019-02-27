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
        float ResolutionScale = 16.0f;
        int MinResolution = 8;
        int MaxResolution = 1024;
        struct RawMapSet
        {
            RawObjectSpaceMap lightmap, diffuseMap, normalMap, positionMap;
            void Init(int w, int h)
            {
                lightmap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
                diffuseMap.Init(RawObjectSpaceMap::DataType::RGBA8, w, h);
                normalMap.Init(RawObjectSpaceMap::DataType::RGB10_X2_SIGNED, w, h);
                positionMap.Init(RawObjectSpaceMap::DataType::RGB32F, w, h);
            }
        };
        EnumerableDictionary<String, RawMapSet> maps;
    private:
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
                    int resolution = Math::Clamp(1 << Math::Log2Ceil((int)(size * ResolutionScale)), MinResolution, MaxResolution);
                    maps[smActor->Name] = RawMapSet();
                    maps[smActor->Name]().Init(resolution, resolution);
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
                auto map = maps.TryGetValue(actor.Key);
                if (!map) continue;
                Texture2D* texDiffuse = hwRenderer->CreateTexture2D(TextureUsage::ColorAttachment, map->diffuseMap.Width, map->diffuseMap.Height, 0, StorageFormat::RGBA_8);
                Texture2D* texPosition = hwRenderer->CreateTexture2D(TextureUsage::ColorAttachment, map->positionMap.Width, map->positionMap.Height, 0, StorageFormat::RGBA_F32);
                Texture2D* texNormal = hwRenderer->CreateTexture2D(TextureUsage::ColorAttachment, map->normalMap.Width, map->normalMap.Height, 0, StorageFormat::RGBA_F32);
                Array<Texture2D*, 3> dest;
                Array<StorageFormat, 3> formats;
                dest.SetSize(3);
                dest[0] = texDiffuse;
                dest[1] = texPosition;
                dest[2] = texNormal;
                formats.SetSize(3);
                formats[0] = StorageFormat::RGBA_8;
                formats[1] = StorageFormat::RGBA_F32;
                formats[2] = StorageFormat::RGBA_F32;
                renderer->RenderObjectSpaceMap(dest.GetArrayView(), formats.GetArrayView(), actor.Value.Ptr());
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
    public:
        void BakeLightmaps(EnumerableDictionary<String, RawObjectSpaceMap>& lightmaps, Level* pLevel)
        {
            level = pLevel;
            AllocLightmaps();
            BakeLightmapGBuffers();
            staticScene = BuildStaticScene(level);

            RefPtr<StaticSceneRenderer> staticRenderer = CreateStaticSceneRenderer();
            staticRenderer->SetCamera(pLevel->CurrentCamera->GetCameraTransform(), pLevel->CurrentCamera->FOV, 1280, 720);
            EnumerableDictionary<String, RawObjectSpaceMap> diffuseMaps;
            for (auto & map : maps)
                diffuseMaps[map.Key] = map.Value.diffuseMap;
            auto & image = staticRenderer->Render(staticScene.Ptr(), diffuseMaps);
            image.GetImageRef().SaveAsBmpFile("StaticSceneRender.bmp");
        }
    };

    void BakeLightmaps(EnumerableDictionary<String, RawObjectSpaceMap>& lightmaps, Level* pLevel)
    {
        LightmapBaker baker;
        baker.BakeLightmaps(lightmaps, pLevel);
    }
}