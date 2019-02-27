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

        CoreLib::Imaging::BitmapF frameBuffer;
        void GetCameraRay(Ray & r, float x, float y)
        {
            float centerX = frameBuffer.GetWidth() * 0.5f;
            float centerY = frameBuffer.GetHeight() * 0.5f;
            
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
            cameraTransform = camTransform;
            fov = screenFov;
            frameBuffer = CoreLib::Imaging::BitmapF(screenWidth, screenHeight);
            screenZ = -(screenHeight >> 1) / tanf(screenFov*(0.5f*PI / 180.0f));
            camRight.SetZero();
            camUp.SetZero();
            camRight.x = 1.0f;
            camUp.y = 1.0f;
        }
        virtual CoreLib::Imaging::BitmapF& Render(StaticScene* scene, CoreLib::EnumerableDictionary<CoreLib::String, RawObjectSpaceMap> & maps) override
        {
            for (int i = 0; i < frameBuffer.GetHeight(); i++)
                for (int j = 0; j < frameBuffer.GetWidth(); j++)
                {
                    Ray ray;
                    GetCameraRay(ray, (float)j, (float)i);
                    auto inter = scene->TraceRay(ray);
                    if (inter.IsHit)
                    {
                        auto map = maps.TryGetValue(inter.Actor->Name.GetValue());
                        if (map)
                        {
                            frameBuffer.GetPixels()[i*frameBuffer.GetHeight() + j] = map->Sample(inter.UV);
                        }
                        else
                        {
                            frameBuffer.GetPixels()[i*frameBuffer.GetHeight() + j] = Vec4::Create(1.0f, 0.0f, 0.0f, 1.0f);
                        }
                    }
                    else
                        frameBuffer.GetPixels()[i*frameBuffer.GetHeight() + j].SetZero();
                }
            return frameBuffer;
        }
    };

    StaticSceneRenderer* CreateStaticSceneRenderer()
    {
        return new StaticSceneRendererImpl();
    }
}