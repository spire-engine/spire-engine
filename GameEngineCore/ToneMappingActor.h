#ifndef GAME_ENGINE_TONE_MAPPING_ACTOR_H
#define GAME_ENGINE_TONE_MAPPING_ACTOR_H

#include "Actor.h"
#include "ToneMapping.h"
#include "EyeAdaptation.h"

namespace GameEngine
{
	class ToneMappingActor : public Actor
	{
	private:
        void ColorLUT_Changing(CoreLib::String & newFileName);
	public:
        PROPERTY_DEF(float, Exposure, 1.0f);
        PROPERTY_DEF(float, MinLuminancePercentile, 0.7f);
        PROPERTY_DEF(float, MaxLuminancePercentile, 0.95f);
        PROPERTY_DEF(float, MinLuminance, 0.1f);
        PROPERTY_DEF(float, MaxLuminance, 5.0f);
        PROPERTY_DEF(float, AdaptSpeedUp, 1.5f);
        PROPERTY_DEF(float, AdaptSpeedDown, 3.5f);
        PROPERTY_ATTRIB(CoreLib::String, ColorLUT, "resource(Texture, clut)");
		ToneMappingParameters GetToneMappingParameters();
        EyeAdaptationUniforms GetEyeAdaptationParameters();
		CoreLib::UniquePtr<Texture3D> lookupTexture;
		virtual CoreLib::String GetTypeName() override
		{
			return "ToneMapping";
		}
		ToneMappingActor()
		{
			Bounds.Init();
		}
		virtual EngineActorType GetEngineType() override
		{
			return EngineActorType::ToneMapping;
		}
	protected:
		bool LoadColorLookupTexture(CoreLib::String fileName);
        virtual void OnLoad() override;
        ~ToneMappingActor();
	};
}

#endif