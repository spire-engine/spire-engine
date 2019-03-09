#include "DeviceLightmapSet.h"
#include "CoreLib/PerformanceCounter.h"
#include "CoreLib/DebugAssert.h"

using namespace CoreLib;

namespace GameEngine
{
    void DeviceLightmapSet::Init(HardwareRenderer* hwRenderer, LightmapSet & lightmapSet)
    {
        int arraySize = Math::Log2Ceil(MaxDeviceLightmapResolution) + 1;
        List<int> imageCounts;
        imageCounts.SetSize(arraySize);
        memset(imageCounts.Buffer(), 0, sizeof(int)*arraySize);
        List<uint32_t> lightmapIds;
        for (int i = 0; i < lightmapSet.Lightmaps.Count(); i++)
        {
            auto & lm = lightmapSet.Lightmaps[i];
            int level = Math::Log2Ceil(lm.Width);
            CoreLib::Diagnostics::DynamicAssert("Lightmaps must be power-of-two-sized.", lm.Width == lm.Height && (1 << level) == lm.Width);
            if (level >= imageCounts.Count())
                throw InvalidOperationException("Lightmap size exceeds maximum limit.");
            int &id = imageCounts[level];
            uint32_t lightmapId = (level << 24) + id;
            id++;
            lightmapIds.Add(lightmapId);
        }
        for (auto & kv : lightmapSet.ActorLightmapIds)
            deviceLightmapIds[kv.Key] = lightmapIds[kv.Value];

        for (auto & tex : textureArrays)
        {
            delete tex;
            tex = nullptr;
        }
        textureArrays.SetSize(arraySize);
        for (int i = 0; i < arraySize; i++)
        {
            int size = 1 << i;
            if (imageCounts[i] != 0)
            {
                textureArrays[i] = hwRenderer->CreateTexture2DArray(TextureUsage::Sampled, size, size, imageCounts[i], 1, StorageFormat::RGBA_F16);
            }
            else
                textureArrays[i] = nullptr;
        }
        CoreLib::List<unsigned short> translatedData;
        for (int i = 0; i < lightmapIds.Count(); i++)
        {
            int level = lightmapIds[i] >> 24;
            int id = lightmapIds[i] & 0xFFFFFF;
            int size = (1 << level);
            auto & srcLightmap = lightmapSet.Lightmaps[i];
            int pixId = 0;
            if (srcLightmap.GetDataType() != RawObjectSpaceMap::DataType::RGBA16F)
            {
                translatedData.SetSize(size * size * 4);
                for (int y = 0; y < size; y++)
                {
                    for (int x = 0; x < size; x++)
                    {
                        auto pix = srcLightmap.GetPixel(x, y);
                        translatedData[pixId * 4] = FloatToHalf(pix.x);
                        translatedData[pixId * 4 + 1] = FloatToHalf(pix.y);
                        translatedData[pixId * 4 + 2] = FloatToHalf(pix.z);
                        translatedData[pixId * 4 + 3] = FloatToHalf(pix.w);
                        pixId++;
                    }
                }
                textureArrays[level]->SetData(0, 0, 0, id, (1 << level), (1 << level), 1, DataType::Half4, translatedData.Buffer());
            }
            else
            {
                textureArrays[level]->SetData(0, 0, 0, id, (1 << level), (1 << level), 1, DataType::Half4, srcLightmap.GetBuffer());
            }
        }
    }

}