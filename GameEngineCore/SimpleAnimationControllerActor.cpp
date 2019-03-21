#include "SimpleAnimationControllerActor.h"
#include "Level.h"
#include "Engine.h"

namespace GameEngine
{
    void SimpleAnimationControllerActor::UpdateStates()
    {
        Tick();
    }
    void SimpleAnimationControllerActor::AnimationFileName_Changing(CoreLib::String & newFileName)
    {
        simpleAnimation = level->LoadSkeletalAnimation(newFileName);
        if (!simpleAnimation)
            newFileName = "";
        UpdateStates();
    }

    void SimpleAnimationControllerActor::SkeletonFileName_Changing(CoreLib::String & newFileName)
    {
        skeleton = level->LoadSkeleton(newFileName);
        if (!skeleton)
            newFileName = "";
        UpdateStates();
    }
    void SimpleAnimationControllerActor::EvalAnimation(float time)
    {
		//if (Engine::Instance()->GetTimingMode() == TimingMode::Fixed && time * simpleAnimation->Speed > simpleAnimation->Duration)
		//{
		//	Engine::Instance()->RequestExit();
		//	return;
		//}
        if (simpleAnimation)
        {
		    Pose p;
            p.Transforms.SetSize(skeleton->Bones.Count());
            float animTime = fmod(time * simpleAnimation->Speed, simpleAnimation->Duration);
            for (int i = 0; i < skeleton->Bones.Count(); i++)
                p.Transforms[i] = skeleton->Bones[i].BindPose;
            for (int i = 0; i < simpleAnimation->Channels.Count(); i++)
            {
                if (simpleAnimation->Channels[i].BoneId == -1)
                    skeleton->BoneMapping.TryGetValue(simpleAnimation->Channels[i].BoneName, simpleAnimation->Channels[i].BoneId);
                if (simpleAnimation->Channels[i].BoneId != -1)
                {
                    p.Transforms[simpleAnimation->Channels[i].BoneId] = simpleAnimation->Channels[i].Sample(animTime);
                }
            }

            for (int i = 0; i < TargetActors->Count(); i++)
            {
                if (auto target = GetTargetActor(i))
                    target->SetPose(p);
            }
        }
    }
    void SimpleAnimationControllerActor::OnLoad()
    {
        AnimationControllerActor::OnLoad();

		if (AnimationFile.GetValue().Length())
			simpleAnimation = level->LoadSkeletalAnimation(*AnimationFile);
		else
			throw CoreLib::ArgumentException("The animation file path is not defined.");

        if (SkeletonFile.GetValue().Length())
            skeleton = level->LoadSkeleton(*SkeletonFile);
		else
			throw CoreLib::ArgumentException("The skeleton file path is not defined.");

        UpdateStates();
        AnimationFile.OnChanging.Bind(this, &SimpleAnimationControllerActor::AnimationFileName_Changing);
        SkeletonFile.OnChanging.Bind(this, &SimpleAnimationControllerActor::SkeletonFileName_Changing);
    }
}
