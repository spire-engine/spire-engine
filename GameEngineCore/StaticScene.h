#ifndef GAME_ENGINE_STATIC_SCENE_H
#define GAME_ENGINE_STATIC_SCENE_H

#include "CoreLib/Basic.h"
#include "Ray.h"
#include "VectorMath.h"

namespace GameEngine
{
    class Actor;
    class Level;

    struct StaticSceneTracingResult
    {
        VectorMath::Vec2 UV;
        int MapId;
        float T = FLT_MAX;
        bool IsHit = false;
        bool CastShadow = true;
        VectorMath::Vec3 Normal;
    };

    enum class StaticLightType
    {
        Directional, Point, Spot
    };

    struct StaticLight
    {
        StaticLightType Type;
        VectorMath::Vec3 Position, Direction, Intensity;
        float SpotFadingStartAngle, SpotFadingEndAngle, Radius;
        bool IncludeDirectLighting;
        bool EnableShadows;
    };

    class StaticScene : public CoreLib::RefObject
    {
    public:
        CoreLib::List<StaticLight> lights;
        VectorMath::Vec3 ambientColor;
        virtual StaticSceneTracingResult TraceRay(const Ray & ray) = 0;
    };

    StaticScene* BuildStaticScene(Level* level);
}

#endif