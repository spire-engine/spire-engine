#ifndef GAME_ENGINE_SHADER_COMPILER_H
#define GAME_ENGINE_SHADER_COMPILER_H

#include "CoreLib/Basic.h"
#include "HardwareRenderer.h"

namespace GameEngine
{
	class ShaderCompilationError
	{
	public:
		CoreLib::String Message;
		CoreLib::String FileName;
		int Line, Col;
	};

	struct DescriptorSetInfo
	{
		CoreLib::List<DescriptorLayout> Descriptors;
		int BindingPoint;
		CoreLib::String BindingName;
	};

    enum class ShaderVariableType
    {
        Data, Texture, StorageBuffer, UniformBuffer, Sampler
    };

    struct ShaderVariableLayout
    {
        CoreLib::String Name;
        int BindingOffset, BindingLength;
        int BindingSpace;
        ShaderVariableType Type;
    };

    struct ShaderTypeSymbol : public CoreLib::RefObject
    {
    public:
        CoreLib::String TypeName;
        CoreLib::String FileName;
        uint32_t TypeId;
        int UniformBufferSize = 0;
        CoreLib::EnumerableDictionary<CoreLib::String, CoreLib::RefPtr<ShaderVariableLayout>> VarLayouts;
        bool HasMember(CoreLib::String memberName)
        {
            return VarLayouts.ContainsKey(memberName);
        }
    };

    struct ShaderEntryPoint : public CoreLib::RefObject 
    {
        CoreLib::String FileName;
        CoreLib::String FunctionName;
        uint32_t Id;
    };

	class ShaderCompilationResult
	{
	public:
        TargetShadingLanguage Language;
		CoreLib::List<char> ShaderCode;
		CoreLib::List<ShaderCompilationError> Diagnostics;
		CoreLib::EnumerableDictionary<CoreLib::String, DescriptorSetInfo> BindingLayouts;
	};

    class ShaderCompilationEnvironment : public CoreLib::RefObject
    {
    public:
        CoreLib::List<ShaderTypeSymbol*> SpecializationTypes;
    };

    class IShaderCompiler : public CoreLib::RefObject
    {
    public:
        virtual bool CompileShader(ShaderCompilationResult & src,
            TargetShadingLanguage targetLang,
            ShaderType shaderType,
            const ShaderEntryPoint* entryPoint,
            const ShaderCompilationEnvironment* env = nullptr) = 0;
        virtual ShaderTypeSymbol* LoadSystemTypeSymbol(CoreLib::String TypeName) = 0;
        virtual ShaderTypeSymbol* LoadTypeSymbol(CoreLib::String fileName, CoreLib::String TypeName) = 0;
        virtual ShaderTypeSymbol* CreateTypeSymbol(CoreLib::String TypeName, CoreLib::String src) = 0;
        virtual ShaderTypeSymbol* LookupTypeSymbol(CoreLib::String TypeName) = 0;
        virtual ShaderEntryPoint* LoadShaderEntryPoint(CoreLib::String fileName, CoreLib::String functionName) = 0;
    };

    IShaderCompiler* CreateShaderCompiler();
    class ShaderSet
    {
    public:
        CoreLib::RefPtr<Shader> vertexShader, fragmentShader;
        operator bool()
        {
            return vertexShader && fragmentShader;
        }
    };
    ShaderSet CompileShader(HardwareRenderer * hw, CoreLib::String fileName);
}

#endif