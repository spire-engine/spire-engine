#include "ToneMappingActor.h"
#include "CoreLib/LibIO.h"
#include "Engine.h"

using namespace CoreLib;

namespace GameEngine
{
    ToneMappingParameters ToneMappingActor::GetToneMappingParameters()
    {
        ToneMappingParameters rs {};
        rs.Exposure = Exposure.GetValue();
        rs.lookupTexture = lookupTexture.Ptr();
        return rs;
    }
    EyeAdaptationUniforms ToneMappingActor::GetEyeAdaptationParameters()
    {
        EyeAdaptationUniforms rs{};
        rs.adaptSpeed[0] = AdaptSpeedUp.GetValue();
        rs.adaptSpeed[1] = AdaptSpeedDown.GetValue();
        rs.maxLuminance = MaxLuminance.GetValue();
        rs.minLuminance = MinLuminance.GetValue();
        return rs;
    }
    void ToneMappingActor::ColorLUT_Changing(CoreLib::String & newFileName)
    {
        if (!LoadColorLookupTexture(newFileName))
            newFileName = "";
    }
    bool ToneMappingActor::LoadColorLookupTexture(CoreLib::String fileName)
	{
		auto fullFile = Engine::Instance()->FindFile(fileName, ResourceType::Texture);
		if (CoreLib::IO::File::Exists(fullFile))
		{
			CoreLib::IO::BinaryReader reader(new CoreLib::IO::FileStream(fullFile));
			int size = reader.ReadInt32();
			List<int> buffer;
			buffer.SetSize(size*size*size);
			reader.Read(buffer.Buffer(), buffer.Count());
			auto hw = Engine::Instance()->GetRenderer()->GetHardwareRenderer();
            hw->Wait();
			lookupTexture = hw->CreateTexture3D(fileName, TextureUsage::Sampled, size, size, size, 1, StorageFormat::RGBA_8);
			lookupTexture->SetData(0, 0, 0, 0, size, size, size, DataType::Byte4, buffer.Buffer());
            return true;
		}
        return false;
	}
    void ToneMappingActor::OnLoad()
    {
        Actor::OnLoad();
        ColorLUT.OnChanging.Bind(this, &ToneMappingActor::ColorLUT_Changing);
        String fileName = ColorLUT.GetValue();
        ColorLUT_Changing(fileName);
    }
    ToneMappingActor::~ToneMappingActor()
    {
        Engine::Instance()->GetRenderer()->Wait();
        lookupTexture = nullptr;
        Engine::Instance()->GetRenderer()->Wait();
    }
}
