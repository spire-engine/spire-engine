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
        Actor* ActorID;
        float T;
        bool IsHit = false;
    };

    class StaticScene : public CoreLib::RefObject
    {
    public:
        virtual StaticSceneTracingResult TraceRay(const Ray & ray) = 0;
    };

    StaticScene* BuildStaticScene(Level* level);
}