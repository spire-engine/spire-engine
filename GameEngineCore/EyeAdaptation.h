#ifndef GAME_ENGINE_EYE_ADAPTATION_H
#define GAME_ENGINE_EYE_ADAPTATION_H

namespace GameEngine
{
    struct EyeAdaptationUniforms
    {
        int histogramSize = 0;
        int width = 0, height = 0;
        int frameId = 0;
        float adaptSpeed[2] = {1.4f, 3.5f};  // x = up, y = down
        float deltaTime = 0.1f;
        float minLuminance = 0.1f, maxLuminance = 5.0f;
    };
}

#endif