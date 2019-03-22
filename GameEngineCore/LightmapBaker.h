#ifndef GAME_ENGINE_LIGHTMAP_BAKER_H
#define GAME_ENGINE_LIGHTMAP_BAKER_H

#include "CoreLib/Basic.h"
#include "CoreLib/Events.h"
#include "LightmapSet.h"

namespace GameEngine
{
    class Level;
    class Mesh;

    struct LightmapBakingSettings
    {
        float ResolutionScale = 1.0f;
        int MinResolution = 32;
        int MaxResolution = 1024;
        int IndirectLightingBounces = 8;
        int SampleCount = 16;
        int FinalGatherSampleCount = 128;
        float FinalGatherAdaptiveSampleThreshold = 0.01f;
        float Epsilon = 1e-5f;
        float ShadowBias = 1e-2f;
        float IndirectLightingWorldGranularity = 30.0f;
    };
    struct LightmapBakerProgressChangedEventArgs
    {
        int ProgressMax;
        int ProgressValue;
        LightmapBakerProgressChangedEventArgs() {}
        LightmapBakerProgressChangedEventArgs(int progress, int progressMax)
        {
            ProgressMax = progressMax;
            ProgressValue = progress;
        }
    };
    class LightmapBaker : public CoreLib::RefObject
    {
    public:
        CoreLib::Event<LightmapBakerProgressChangedEventArgs> OnProgressChanged;
        CoreLib::Event<CoreLib::String> OnStatusChanged;
        CoreLib::Event<> OnIterationCompleted;
        CoreLib::Event<bool /*isCancelled*/> OnCompleted;
        CoreLib::Event<Mesh*> OnMeshChanged;
        virtual void Start(const LightmapBakingSettings & settings, Level* pLevel) = 0;
        virtual bool IsRunning() = 0;
        virtual bool IsCancelled() = 0;
        virtual LightmapSet& GetLightmapSet() = 0;
        virtual void Wait() = 0;
        virtual void Cancel() = 0;
    };
    LightmapBaker* CreateLightmapBaker();
}

#endif