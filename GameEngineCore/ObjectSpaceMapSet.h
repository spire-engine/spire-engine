#ifndef OBJECT_SPACE_MAP_SET_H
#define OBJECT_SPACE_MAP_SET_H

#include "CoreLib/VectorMath.h"
#include "CoreLib/Basic.h"

namespace GameEngine
{
    class RawObjectSpaceMap
    {
    public:
        CoreLib::String ActorName;
        CoreLib::List<unsigned char> Data;
        int Width, Height;
        VectorMath::Vec4 GetPixel(int x, int y);
        void SetPixel(int x, int y, VectorMath::Vec4 value);
    };

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
        CoreLib::EnumerableDictionary<CoreLib::String, CoreLib::RefPtr<RawObjectSpaceMap>> RawMaps;
        DataType GetDataType() { return dataType; }
        void Init(DataType type, Level* level);
        void LoadFromFile(CoreLib::String fileName);
        void SaveToFile(CoreLib::String fileName);
    };
}

#endif