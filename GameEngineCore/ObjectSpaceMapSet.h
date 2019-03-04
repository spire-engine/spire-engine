#ifndef OBJECT_SPACE_MAP_SET_H
#define OBJECT_SPACE_MAP_SET_H

#include "CoreLib/VectorMath.h"
#include "CoreLib/Basic.h"

namespace GameEngine
{
    class RawObjectSpaceMap
    {
    public:
        enum class DataType
        {
            RGBA8, RGB32F, RGB10_X2_SIGNED, RGBA32F
        };
    private:
        DataType dataType;
        CoreLib::List<unsigned char> data;
    public:
        int Width, Height;
        VectorMath::Vec4 GetPixel(int x, int y);
        VectorMath::Vec4 Sample(VectorMath::Vec2 uv);
        void SetPixel(int x, int y, VectorMath::Vec4 value);
        void * GetBuffer() { return data.Buffer(); }
        void Init(DataType type, int w, int h);
        void SaveToFile(CoreLib::String fileName);
    };
    uint32_t PackRGBA8(float x, float y, float z, float w);
    uint32_t PackRGB10(float x, float y, float z);
    VectorMath::Vec3 UnpackRGB10(uint32_t val);
    VectorMath::Vec4 UnpackRGBA8(uint32_t val);
}

#endif