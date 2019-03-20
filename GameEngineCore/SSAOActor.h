#ifndef GAME_ENGINE_SSAO_ACTOR_H
#define GAME_ENGINE_SSAO_ACTOR_H

#include "Actor.h"
#include "SSAO.h"

namespace GameEngine
{
	class SSAOActor : public Actor
	{
	public:
		PROPERTY_DEF(float, Radius, 40.0f);
        PROPERTY_DEF(float, Distance, 20.0f);
        PROPERTY_DEF(float, Power, 1.0f);
		PROPERTY_DEF(int, BlurRadius, 5);

        SSAOUniforms GetParameters();
		virtual EngineActorType GetEngineType() override
		{
			return EngineActorType::SSAO;
		}
		virtual CoreLib::String GetTypeName() override
		{
			return "SSAO";
		}
	};
}

#endif