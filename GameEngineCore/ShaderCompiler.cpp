#include "ShaderCompiler.h"
#include "CoreLib/LibIO.h"
#include "Engine.h"
#include "ExternalLibs/Slang/slang.h"

namespace GameEngine
{
	using namespace CoreLib;
	using namespace CoreLib::IO;

    class SlangShaderCompiler : public IShaderCompiler
    {
    public:
        EnumerableDictionary<String, RefPtr<ShaderEntryPoint>> shaderEntryPoints;
        EnumerableDictionary<String, RefPtr<ShaderTypeSymbol>> shaderTypeSymbols;
        SlangSession *session;
        StringBuilder sb;
        SlangShaderCompiler()
        {
            session = spCreateSession();
        }
        ~SlangShaderCompiler()
        {
            spDestroySession(session);
        }
    public:
        int GetSlangTarget()
        {
            auto sl = Engine::Instance()->GetRenderer()->GetHardwareRenderer()->GetShadingLanguage();
            switch (sl)
            {
            case TargetShadingLanguage::HLSL:
                return SLANG_DXBC;
            case TargetShadingLanguage::SPIRV:
                return SLANG_SPIRV;
            }
            return 0;
        }
        int GetSlangStage(StageFlags t)
        {
            switch (t)
            {
            case sfVertex:
                return SLANG_STAGE_VERTEX;
                break;
            case sfFragment:
                return SLANG_STAGE_FRAGMENT;
                break;
            case sfCompute:
                return SLANG_STAGE_COMPUTE;
                break;
            }
            throw ArgumentException("Unknown shader type.");
        }
        BindingType SlangCategoryToDescriptorType(int c)
        {
            switch (c)
            {
            case SLANG_PARAMETER_CATEGORY_UNIFORM:
                return BindingType::Unused;
            case SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER:
                return BindingType::UniformBuffer;
            case SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE:
                return BindingType::Texture;
            case SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS:
                return BindingType::StorageBuffer;
            case SLANG_PARAMETER_CATEGORY_SAMPLER_STATE:
                return BindingType::Sampler;
            }
            throw ArgumentException("Unknown slang category type.");
        }
        virtual bool CompileShader(ShaderCompilationResult & src,
            const CoreLib::ArrayView<ShaderEntryPoint*> entryPoints,
            const ShaderCompilationEnvironment* env = nullptr) override
        {
            StageFlags stageFlags = sfNone;
            auto req = NewCompileRequest();
            for (auto ep : entryPoints)
            {
                int unit = spAddTranslationUnit(req, GetSlangTarget(), ep->FileName.Buffer());
                auto path = Engine::Instance()->FindFile(ep->FileName, ResourceType::Shader);
                spAddTranslationUnitSourceFile(req, unit, path.Buffer());
                spAddEntryPoint(req, unit, ep->FunctionName.Buffer(), GetSlangStage(ep->Stage));
                stageFlags = StageFlags(stageFlags | ep->Stage);
            }
            if (env)
            {
                for (int i = 0; i < env->SpecializationTypes.Count(); i++)
                {
                    spAddPreprocessorDefine(req, (String("SPECIALIZATION_TYPE_") + i).Buffer(), env->SpecializationTypes[i]->TypeName.Buffer());
                }
            }
            int anyErrors = spCompile(req);
            if (anyErrors)
            {
                const char* diagMsg = spGetDiagnosticOutput(req);
                Print("Error compiling shader.\n%s\n", diagMsg);
                spDestroyCompileRequest(req);
                src.Diagnostics = diagMsg;
                return false;
            }
            src.ShaderCode.SetSize(entryPoints.Count());
            for (int i = 0; i < entryPoints.Count(); i++)
            {
                size_t size;
                char * code = (char*)spGetEntryPointCode(req, i, &size);
                src.ShaderCode[i].SetSize((int)size);
                memcpy(src.ShaderCode[i].Buffer(), code, size);
            }
            slang::ShaderReflection * reflection = slang::ShaderReflection::get(req);
            auto entryPoint = reflection->getEntryPointByIndex(0);
            int paramCount = (int)entryPoint->getParameterCount();
            EnumerableDictionary<int, DescriptorSetInfo> descSets;
            for (int i = 0; i < paramCount; i++)
            {
                auto param = entryPoint->getParameterByIndex(i);
                int space = param->getBindingSpace();
                auto descSet = descSets.TryGetValue(space);
                if (!descSet)
                {
                    descSets.Add(space, DescriptorSetInfo());
                    descSet = descSets.TryGetValue(space);
                    descSet->BindingPoint = space;
                }
                if (param->getCategory() != slang::Uniform)
                {
                    DescriptorLayout desc;
                    desc.Location = param->getBindingIndex();
                    desc.Type = SlangCategoryToDescriptorType(param->getCategory());
                    desc.Stages = stageFlags;
                    descSet->Descriptors.Add(desc);
                }
            }
            spDestroyCompileRequest(req);
            return true;
        }
        virtual ShaderTypeSymbol* LoadSystemTypeSymbol(CoreLib::String TypeName) override
        {
            return LoadTypeSymbol("ShaderLib.slang", TypeName);
        }
        SlangCompileRequest* NewCompileRequest()
        {
            auto compileRequest = spCreateCompileRequest(session);
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(true, ResourceType::Shader).Buffer());
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(false, ResourceType::Shader).Buffer());
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(true, ResourceType::Material).Buffer());
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(false, ResourceType::Material).Buffer());
            
            spAddCodeGenTarget(compileRequest, GetSlangTarget());
            int shaderLibUnit = spAddTranslationUnit(compileRequest, SLANG_SOURCE_LANGUAGE_SLANG, "ShaderLib");
            auto shaderLibPath = Engine::Instance()->FindFile("ShaderLib.slang", ResourceType::Shader);
            spAddTranslationUnitSourceFile(compileRequest, shaderLibUnit, shaderLibPath.Buffer());
            return compileRequest;
        }
        virtual ShaderTypeSymbol* LoadTypeSymbol(CoreLib::String fileName, CoreLib::String TypeName) override
        {
            sb.Clear();
            sb << fileName << "/" << TypeName;
            auto str = sb.ProduceString();
            RefPtr<ShaderTypeSymbol> sym;
            if (shaderTypeSymbols.TryGetValue(str, sym))
                return sym.Ptr();
            sym = new ShaderTypeSymbol();
            sym->FileName = fileName;
            sym->TypeId = shaderTypeSymbols.Count();
            sym->TypeName = TypeName;
            auto compileRequest = NewCompileRequest();
            if (fileName != "ShaderLib.slang")
            {
                auto shaderFile = Engine::Instance()->FindFile(fileName, ResourceType::Shader);
                int unit = spAddTranslationUnit(compileRequest, SLANG_SOURCE_LANGUAGE_SLANG, fileName.Buffer());
                spAddTranslationUnitSourceFile(compileRequest, unit, shaderFile.Buffer());
            }
            int anyErrors = spCompile(compileRequest);
            if (anyErrors)
            {
                const char* diagMsg = spGetDiagnosticOutput(compileRequest);
                Print("Error compiling shader %s. Compiler output:\n%s\n", fileName.Buffer(), diagMsg);
                spDestroyCompileRequest(compileRequest);
                return nullptr;
            }
            slang::ShaderReflection* shaderReflection = slang::ShaderReflection::get(compileRequest);
            auto typeReflection = shaderReflection->findTypeByName(TypeName.Buffer());
            if (!typeReflection)
            {
                Print("Type %s not found in compiled shader library %s.\n", TypeName.Buffer(), fileName.Buffer());
                spDestroyCompileRequest(compileRequest);
                return nullptr;
            }
            auto typeLayout = shaderReflection->getTypeLayout(typeReflection);
            for (uint32_t i = 0u; i < typeLayout->getFieldCount(); i++)
            {
                ShaderVariableLayout varLayout;
                auto field = typeLayout->getFieldByIndex(i);
                varLayout.Name = field->getName();
                varLayout.BindingOffset = field->getBindingIndex();
                varLayout.BindingSpace = field->getBindingSpace();
                varLayout.BindingLength = field->getCategoryCount();
                switch (field->getCategory())
                {
                case slang::ConstantBuffer:
                    varLayout.Type = ShaderVariableType::UniformBuffer;
                    break;
                case slang::Uniform:
                    varLayout.BindingOffset = (int)field->getOffset();
                    varLayout.Type = ShaderVariableType::Data;
                    break;
                case slang::SamplerState:
                    varLayout.Type = ShaderVariableType::Sampler;
                    break;
                case slang::ShaderResource:
                    varLayout.Type = ShaderVariableType::Texture;
                    break;
                case slang::UnorderedAccess:
                    varLayout.Type = ShaderVariableType::StorageBuffer;
                    break;
                }
                sym->VarLayouts.Add(varLayout.Name, varLayout);
            }
            sym->UniformBufferSize = (int)typeLayout->getSize();
            shaderTypeSymbols[str] = sym;
            spDestroyCompileRequest(compileRequest);
            return sym.Ptr();
        }
        virtual ShaderEntryPoint* LoadShaderEntryPoint(CoreLib::String fileName, CoreLib::String functionName) override
        {
            sb.Clear();
            sb << fileName << "/" << functionName;
            auto str = sb.ProduceString();
            RefPtr<ShaderEntryPoint> entryPoint;
            if (shaderEntryPoints.TryGetValue(str, entryPoint))
                return entryPoint.Ptr();
            entryPoint = new ShaderEntryPoint();
            entryPoint->FileName = fileName;
            entryPoint->FunctionName = functionName;
            entryPoint->Id = shaderEntryPoints.Count();
            if (functionName.StartsWith("vs_"))
                entryPoint->Stage = sfVertex;
            else if (functionName.StartsWith("ps_"))
                entryPoint->Stage = sfFragment;
            else if (functionName.StartsWith("cs_"))
                entryPoint->Stage = sfCompute;
            else
                throw InvalidOperationException("Invalid shader entrypoint name. Must start with 'vs_', 'ps_' or 'cs_'.");
            shaderEntryPoints.Add(str, entryPoint);
            return entryPoint.Ptr();
        }
    };

    IShaderCompiler* CreateShaderCompiler()
    {
        return nullptr;
    }

    ShaderSet CompileGraphicsShader(HardwareRenderer * hw, String fileName)
    {
        ShaderCompilationResult crs;
        Array<ShaderEntryPoint*, 2> entryPoints;
        entryPoints.SetSize(2);
        entryPoints[0] = Engine::GetShaderCompiler()->LoadShaderEntryPoint(fileName, "vs_main");
        entryPoints[1] = Engine::GetShaderCompiler()->LoadShaderEntryPoint(fileName, "ps_main");
        bool succ = Engine::GetShaderCompiler()->CompileShader(crs, entryPoints.GetArrayView());
        
        ShaderSet set;
        if (succ)
        {
            set.vertexShader = hw->CreateShader(ShaderType::VertexShader, crs.ShaderCode[0].Buffer(), crs.ShaderCode[0].Count());
            set.fragmentShader = hw->CreateShader(ShaderType::FragmentShader, crs.ShaderCode[1].Buffer(), crs.ShaderCode[1].Count());
        }
        return set;
    }
}