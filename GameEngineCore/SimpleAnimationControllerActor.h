#ifndef GAME_ENGINE_SIMPLE_ANIMATION_CONTROLLER_ACTOR
#define GAME_ENGINE_SIMPLE_ANIMATION_CONTROLLER_ACTOR

#include "AnimationControllerActor.h"
#include "VectorMath.h"

namespace GameEngine
{
    class SimpleAnimationControllerActor : public AnimationControllerActor
    {
    protected:
        SkeletalAnimation * simpleAnimation = nullptr;
        Skeleton * skeleton = nullptr;
        virtual void EvalAnimation(float time) override;
        void UpdateStates();
        void AnimationFileName_Changing(CoreLib::String & newFileName);
        void SkeletonFileName_Changing(CoreLib::String & newFileName);

    public:
        PROPERTY_ATTRIB(CoreLib::String, AnimationFile, "resource(Animation, anim)");
        PROPERTY_ATTRIB(CoreLib::String, SkeletonFile, "resource(Animation, skeleton)");

        virtual CoreLib::String GetTypeName() override
        {
            return "SimpleAnimationController";
        }
        virtual void OnLoad();
    };
}

#endif