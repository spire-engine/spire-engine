#include "ObjectSpaceMapSet.h"
#include "CoreLib/Basic.h"
#include "CoreLib/VectorMath.h"
#include "CoreLib/Imaging/Bitmap.h"

using namespace CoreLib;

namespace GameEngine
{
    int GetElementSize(RawObjectSpaceMap::DataType t)
    {
        switch (t)
        {
        case RawObjectSpaceMap::DataType::RGB10_X2_SIGNED:
        case RawObjectSpaceMap::DataType::RGBA8:
            return 4;
        case RawObjectSpaceMap::DataType::RGB32F:
            return 12;
        case RawObjectSpaceMap::DataType::RGBA32F:
            return 16;
        case RawObjectSpaceMap::DataType::RGBA16F:
            return 8;
        case RawObjectSpaceMap::DataType::BC6H:
            return 1;
        }
        return 0;
    }
    VectorMath::Vec4 RawObjectSpaceMap::GetPixel(int x, int y)
    {
        switch (dataType)
        {
        case RawObjectSpaceMap::DataType::RGB10_X2_SIGNED:
            return VectorMath::Vec4::Create(UnpackRGB10(((uint32_t*)data.Buffer())[y*Height + x]), 0.0f);
        case RawObjectSpaceMap::DataType::RGBA8:
            return UnpackRGBA8(((uint32_t*)data.Buffer())[y*Height + x]);
        case RawObjectSpaceMap::DataType::RGB32F:
            return VectorMath::Vec4::Create(((VectorMath::Vec3*)data.Buffer())[y*Height + x], 1.0f);
        case RawObjectSpaceMap::DataType::RGBA32F:
            return ((VectorMath::Vec4*)data.Buffer())[y*Height + x];
        case RawObjectSpaceMap::DataType::RGBA16F:
        {
            auto ptr = (unsigned short*)(data.Buffer() + (y * Height + x) * 8);
            return VectorMath::Vec4::Create(HalfToFloat(ptr[0]), HalfToFloat(ptr[1]), HalfToFloat(ptr[2]), HalfToFloat(ptr[3]));
        }
        }
        return VectorMath::Vec4::Create(0.0f);
    }
    VectorMath::Vec4 RawObjectSpaceMap::Sample(VectorMath::Vec2 uv)
    {
        int x = Math::Clamp((int)(uv.x * Width), 0, Width - 1);
        int y = Math::Clamp((int)(uv.y * Height), 0, Height - 1);
        return GetPixel(x, y);
    }
    void RawObjectSpaceMap::SetPixel(int x, int y, VectorMath::Vec4 value)
    {
        switch (dataType)
        {
        case RawObjectSpaceMap::DataType::RGB10_X2_SIGNED:
            ((uint32_t*)data.Buffer())[y*Height + x] = PackRGB10(value.x, value.y, value.z);
            break;
        case RawObjectSpaceMap::DataType::RGBA8:
            ((uint32_t*)data.Buffer())[y*Height + x] = PackRGBA8(value.x, value.y, value.z, value.w);
            break;
        case RawObjectSpaceMap::DataType::RGB32F:
            ((VectorMath::Vec3*)data.Buffer())[y*Height + x] = value.xyz();
            break;
        case RawObjectSpaceMap::DataType::RGBA32F:
            ((VectorMath::Vec4*)data.Buffer())[y*Height + x] = value;
            break;
        case RawObjectSpaceMap::DataType::RGBA16F:
        {
            auto ptr = (unsigned short*)(data.Buffer() + (y * Height + x) * 8);
            ptr[0] = FloatToHalf(value.x);
            ptr[1] = FloatToHalf(value.y);
            ptr[2] = FloatToHalf(value.z);
            ptr[3] = FloatToHalf(value.w);
            break;
        }
        }
    }
    void RawObjectSpaceMap::Init(DataType type, int w, int h)
    {
        dataType = type;
        int elementSize = GetElementSize(type);
        data.SetSize(elementSize*w*h);
        memset(data.Buffer(), 0, data.Count());
        Width = w;
        Height = h;
    }
    void RawObjectSpaceMap::DebugSaveAsImage(String fileName)
    {
        CoreLib::Imaging::BitmapF bmp = CoreLib::Imaging::BitmapF(Width, Height);
        auto pixels = bmp.GetPixels();
        for (int i = 0; i < Width * Height; i++)
            pixels[i] = GetPixel(i % Width, i / Width);
        bmp.GetImageRef().SaveAsPfmFile(fileName);
    }

    struct TextureMapFileHeader
    {
        char identifier[4] = { 'T', 'E', 'X', 'M' };
        unsigned int Size = 0;
        int Version = 0;
        int Width, Height;
        RawObjectSpaceMap::DataType DataType;
        int Reserved[16] = {};
    };

    void RawObjectSpaceMap::SaveToStream(CoreLib::IO::BinaryWriter& writer)
    {
        TextureMapFileHeader header;
        header.Width = Width;
        header.Height = Height;
        header.DataType = dataType;
        header.Size = sizeof(TextureMapFileHeader) + data.Count();
        writer.Write(header);
        writer.Write(data.Buffer(), data.Count());
    }
    void RawObjectSpaceMap::LoadFromStream(CoreLib::IO::BinaryReader& reader)
    {
        TextureMapFileHeader header;
        reader.Read(header);
        if (strncmp(header.identifier, "TEXM", 4) != 0 || sizeof(TextureMapFileHeader) + header.Width*header.Height*GetElementSize(header.DataType) != header.Size)
            throw IO::IOException("Invalid texture data file.");
        Width = header.Width;
        Height = header.Height;
        dataType = header.DataType;
        data.SetSize(header.Width*header.Height*GetElementSize(header.DataType));
        reader.Read(data.Buffer(), data.Count());
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

    uint32_t PackRGBA8(float x, float y, float z, float w)
    {
        auto r1 = CoreLib::Math::Clamp((uint32_t)(x * 255), 0u, 255u);
        auto r2 = CoreLib::Math::Clamp((uint32_t)(y * 255), 0u, 255u);
        auto r3 = CoreLib::Math::Clamp((uint32_t)(z * 255), 0u, 255u);
        auto r4 = CoreLib::Math::Clamp((uint32_t)(w * 255), 0u, 255u);

        return (r4 << 24) + (r3 << 16) + (r2 << 8) + r1;
    }
    VectorMath::Vec4 UnpackRGBA8(uint32_t val)
    {
        float x = (val & 255) / 255.0f;
        float y = ((val >> 8) & 255) / 255.0f;
        float z = ((val >> 16) & 255) / 255.0f;
        float w = ((val >> 24) & 255) / 255.0f;
        return VectorMath::Vec4::Create(x, y, z, w);
    }
}