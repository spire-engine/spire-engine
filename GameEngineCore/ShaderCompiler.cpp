#include "ShaderCompiler.h"
#include "CoreLib/LibIO.h"
#include "Engine.h"

namespace GameEngine
{
	using namespace CoreLib;
	using namespace CoreLib::IO;

    IShaderCompiler* CreateShaderCompiler()
    {
        return nullptr;
    }

    ShaderSet CompileShader(HardwareRenderer * hw, String fileName)
    {
        auto shadingLang = Engine::Instance()->GetRenderer()->GetHardwareRenderer()->GetShadingLanguage();
        ShaderCompilationResult vsRs;
        bool vsSucc = Engine::GetShaderCompiler()->CompileShader(vsRs, shadingLang, ShaderType::VertexShader,
            Engine::GetShaderCompiler()->LoadShaderEntryPoint(fileName, "vs_main"));
        ShaderCompilationResult fsRs;
        bool fsSucc = Engine::GetShaderCompiler()->CompileShader(vsRs, shadingLang, ShaderType::VertexShader,
            Engine::GetShaderCompiler()->LoadShaderEntryPoint(fileName, "ps_main"));

        ShaderSet set;
        if (vsSucc)
            set.vertexShader = hw->CreateShader(ShaderType::VertexShader, vsRs.ShaderCode.Buffer(), vsRs.ShaderCode.Count());
        if (fsSucc)
            set.fragmentShader = hw->CreateShader(ShaderType::FragmentShader, fsRs.ShaderCode.Buffer(), fsRs.ShaderCode.Count());
        return set;
    }
}