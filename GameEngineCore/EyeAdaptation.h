#ifndef GAME_ENGINE_EYE_ADAPTATION_H
#define GAME_ENGINE_EYE_ADAPTATION_H

namespace GameEngine
{
    struct EyeAdaptationUniforms
    {
        int histogramSize;
        int width, height;
        int frameId;
        float adaptSpeed[2];  // x = up, y = down
        float deltaTime;
        float minLuminance, maxLuminance;
    };
}

#endif