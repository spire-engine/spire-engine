#ifndef GAME_ENGINE_LIGHT_ACTOR_H
#define GAME_ENGINE_LIGHT_ACTOR_H

#include "GizmoActor.h"

namespace GameEngine
{
	enum class LightType
	{
		Ambient, Directional, Point
	};
	class LightActor : public GizmoActor
	{
	protected:
		virtual Mesh CreateGizmoMesh() = 0;
	public:
		LightType lightType;
        PROPERTY_DEF_ATTRIB(int, Mobility, 0, "enum(Static,Stationary,Dynamic)");
        PROPERTY_DEF_ATTRIB(int, EnableShadows, 1, "enum(Disabled,Static,Dynamic)");
        PROPERTY_DEF(float, Radius, 0.0f);
		VectorMath::Vec3 GetDirection();
		virtual EngineActorType GetEngineType() override
		{
			return EngineActorType::Light;
		}
		virtual bool ParseField(CoreLib::String, CoreLib::Text::TokenReader &) override;
		virtual void OnLoad() override;
	};
}

#endif