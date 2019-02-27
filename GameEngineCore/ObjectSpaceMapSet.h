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
            RGBA8, RGB32F, RGB10_X2_SIGNED
        };
    private:
        DataType dataType;
        CoreLib::List<unsigned char> data;
    public:
        int Width, Height;
        VectorMath::Vec4 GetPixel(int x, int y);
        void SetPixel(int x, int y, VectorMath::Vec4 value);
        void * GetBuffer() { return data.Buffer(); }
        void Init(DataType type, int w, int h);
    };

    uint32_t PackRGB10(float x, float y, float z);
    VectorMath::Vec3 UnpackRGB10(uint32_t val);

    class Level;

    class ObjectSpaceMapSet : public CoreLib::RefObject
    {
    public:
        enum class DataType
        {
            RGBA8, RGBA32F
        };
    private:
        DataType dataType;
    public:
        CoreLib::EnumerableDictionary<CoreLib::String, RawObjectSpaceMap> RawMaps;
        DataType GetDataType() { return dataType; }
        void Init(DataType type, Level* level);
        void LoadFromFile(CoreLib::String fileName);
        void SaveToFile(CoreLib::String fileName);
    };
}

#endif