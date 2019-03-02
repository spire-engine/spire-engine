#include "StaticSceneRenderer.h"
#include "StaticScene.h"
#include "ObjectSpaceMapSet.h"
#include "CoreLib/Imaging/Bitmap.h"
#include "Actor.h"

namespace GameEngine
{
    using namespace CoreLib;
    using namespace VectorMath;

    class StaticSceneRendererImpl : public StaticSceneRenderer
    {
    private:
        VectorMath::Vec3 camRight, camUp;
        Matrix4 cameraTransform;
        float screenZ;
        float fov;

        CoreLib::RefPtr<CoreLib::Imaging::BitmapF> frameBuffer;
        void GetCameraRay(Ray & r, int w, int h, float x, float y)
        {
            float centerX = w * 0.5f;
            float centerY = h * 0.5f;
            
            Vec3 tmpVec;
            r.tMax = FLT_MAX;
            Vec3::Scale(r.Dir, camRight, (float)(x - centerX));
            Vec3::Scale(tmpVec, camUp, (float)(centerY - y));
            r.Dir += tmpVec;
            r.Dir.z = screenZ;
            Vec3 d = r.Dir;
            float dirDOTdir = Vec3::Dot(r.Dir, r.Dir);
            float dirLength = sqrt(dirDOTdir);
            float invDirLength = 1.0f / dirLength;
            r.Dir *= invDirLength;
            r.Origin.SetZero();
            Vec3 tmp;
            cameraTransform.Transform(tmp, r.Origin); r.Origin = tmp;
            cameraTransform.TransformNormal(tmp, r.Dir); r.Dir = tmp;
        }
    public:
        virtual void SetCamera(VectorMath::Matrix4 camTransform, float screenFov, int screenWidth, int screenHeight) override
        {
            camTransform.Inverse(cameraTransform);
            fov = screenFov;
            frameBuffer = new CoreLib::Imaging::BitmapF(screenWidth, screenHeight);
            screenZ = -(screenHeight >> 1) / tanf(screenFov*(0.5f*PI / 180.0f));
            camRight.SetZero();
            camUp.SetZero();
            camRight.x = 1.0f;
            camUp.y = 1.0f;
        }
        virtual CoreLib::Imaging::BitmapF& Render(StaticScene* scene, CoreLib::List<RawObjectSpaceMap> & diffuseMaps, LightmapSet & lightMaps) override
        {
            auto pixels = frameBuffer->GetPixels();
            int h = frameBuffer->GetHeight();
            int w = frameBuffer->GetWidth();
            #pragma omp parallel for
            for (int i = 0; i < h; i++)
                for (int j = 0; j < w; j++)
                {
                    Ray ray;
                    if (i == 512 && j == 520)
                        printf("break");
                    GetCameraRay(ray, w, h, (float)j, (float)i);
                    auto inter = scene->TraceRay(ray);
                    if (inter.IsHit)
                    {
                        auto & diffuseMap = diffuseMaps[inter.MapId];
                        auto & lightMap = lightMaps.Lightmaps[inter.MapId];
                        pixels[i * w + j] = diffuseMap.Sample(inter.UV) * lightMap.Sample(inter.UV);
                    }
                    else
                        pixels[i * w + j] = Vec4::Create(0.0f, 0.0f, 0.4f, 1.0f);
                }
            return *frameBuffer;
        }
    };

    StaticSceneRenderer* CreateStaticSceneRenderer()
    {
        return new StaticSceneRendererImpl();
    }
}