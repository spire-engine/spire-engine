#include "SSAOActor.h"

namespace GameEngine
{
    SSAOUniforms SSAOActor::GetParameters()
    {
        SSAOUniforms rs;
        rs.aoPower = Power.GetValue();
        rs.aoRadius = Radius.GetValue();
        rs.aoDistance = Distance.GetValue();
        rs.blurRadius = BlurRadius.GetValue();
        return rs;
    }
}
