#include "ObjectSpaceMapSet.h"
#include "CoreLib/Basic.h"
#include "VectorMath.h"

using namespace CoreLib;

namespace GameEngine
{
    VectorMath::Vec4 RawObjectSpaceMap::GetPixel(int x, int y)
    {
        return VectorMath::Vec4();
    }
    void RawObjectSpaceMap::SetPixel(int x, int y, VectorMath::Vec4 value)
    {
    }
    void ObjectSpaceMapSet::Init(DataType type, Level * level)
    {
    }
    void ObjectSpaceMapSet::LoadFromFile(CoreLib::String fileName)
    {
    }
    void ObjectSpaceMapSet::SaveToFile(CoreLib::String fileName)
    {
    }

    uint32_t PackRGB10(float x, float y, float z)
    {
        auto r1 = CoreLib::Math::Clamp((uint32_t)((x + 1.0f) * 0.5f * 1023), 0u, 1023u);
        auto r2 = CoreLib::Math::Clamp((uint32_t)((y + 1.0f) * 0.5f * 1023), 0u, 1023u);
        auto r3 = CoreLib::Math::Clamp((uint32_t)((z + 1.0f) * 0.5f * 1023), 0u, 1023u);
        return (r3 << 20) + (r2 << 10) + r1;
    }
    VectorMath::Vec3 UnpackRGB10(uint32_t val)
    {
        float x = (val & 1023) / 1023.0f * 2.0f - 1.0f;
        float y = ((val>>10) & 1023) / 1023.0f * 2.0f - 1.0f;
        float z = ((val>>20) & 1023) / 1023.0f * 2.0f - 1.0f;
        return VectorMath::Vec3::Create(x, y, z);
    }
}