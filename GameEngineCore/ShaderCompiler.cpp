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
            auto sl = Engine::Instance()->GetTargetShadingLanguage();
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
        BindingType SlangResourceKindToDescriptorType(slang::TypeReflection::Kind k, SlangResourceShape shape)
        {
            switch (k)
            {
            case slang::TypeReflection::Kind::ConstantBuffer:
                return BindingType::UniformBuffer;
            case slang::TypeReflection::Kind::SamplerState:
                return BindingType::Sampler;
            case slang::TypeReflection::Kind::Resource:
                switch (shape)
                {
                case SLANG_STRUCTURED_BUFFER:
                case SLANG_BYTE_ADDRESS_BUFFER:
                    return BindingType::StorageBuffer;
                default:
                    return BindingType::Texture;
                    break;
                }
            }
            throw ArgumentException("Unknown slang resource kind.");
        }
        bool IsResourceParam(int slangCategory)
        {
            switch (slangCategory)
            {
            case SLANG_PARAMETER_CATEGORY_CONSTANT_BUFFER:
            case SLANG_PARAMETER_CATEGORY_SHADER_RESOURCE:
            case SLANG_PARAMETER_CATEGORY_UNORDERED_ACCESS:
            case SLANG_PARAMETER_CATEGORY_SAMPLER_STATE:
                return true;
            }
            return false;
        }
        virtual bool CompileShader(ShaderCompilationResult & src,
            const CoreLib::ArrayView<ShaderEntryPoint*> entryPoints,
            const ShaderCompilationEnvironment* env = nullptr) override
        {
            StageFlags stageFlags = sfNone;
            auto req = NewCompileRequest();
            for (auto ep : entryPoints)
            {
                int unit = spAddTranslationUnit(req, SLANG_SOURCE_LANGUAGE_SLANG, ep->FileName.Buffer());
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
                    spAddPreprocessorDefine(req, (String("IMPORT_MODULE_") + i).Buffer(), Path::GetFileNameWithoutEXT(env->SpecializationTypes[i]->FileName).Buffer());

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
            // extract reflection data
            slang::ShaderReflection * reflection = slang::ShaderReflection::get(req);
            int paramCount = (int)reflection->getParameterCount();
            for (int i = 0; i < paramCount; i++)
            {
                auto param = reflection->getParameterByIndex(i);
                auto paramName = param->getName();
                if (param->getType()->getKind() == slang::TypeReflection::Kind::ParameterBlock)
                {
                    auto layout = param->getTypeLayout();
                    DescriptorSetInfo set;
                    set.BindingPoint = param->getBindingSpace();
                    set.Name = paramName;
                    auto resType = layout->getElementVarLayout()->getTypeLayout();
                    auto reslayout = layout->getElementVarLayout();
                    int slotOffset = (int)reslayout->getOffset(SLANG_PARAMETER_CATEGORY_DESCRIPTOR_TABLE_SLOT);
                    bool hasUniform = false;
                    for (auto f = 0u; f < resType->getFieldCount(); f++)
                    {
                        auto field = resType->getFieldByIndex(f);
                        if (field->getCategory() == slang::Uniform || field->getType()->getKind() == slang::TypeReflection::Kind::Struct)
                        {
                            if (!hasUniform)
                            {
                                DescriptorLayout desc;
                                desc.Location = 0;
                                desc.Type = BindingType::UniformBuffer;
                                desc.Stages = stageFlags;
                                desc.Name = field->getName();
                                set.Descriptors.Add(desc);
                                hasUniform = true;
                            }
                            continue;
                        }
                        DescriptorLayout desc;
                        desc.Location = field->getBindingIndex() + slotOffset;
                        desc.Name = field->getName();
                        if (field->getType()->getKind() == slang::TypeReflection::Kind::Array)
                        {
                            desc.Type = SlangResourceKindToDescriptorType(field->getType()->getElementType()->getKind(), field->getType()->getResourceShape());
                            desc.ArraySize = field->getType()->getElementCount();
                        }
                        else
                        {
                            desc.Type = SlangResourceKindToDescriptorType(field->getType()->getKind(), field->getType()->getResourceShape());
                        }
                        desc.Stages = stageFlags;
                        set.Descriptors.Add(desc);
                    }
                    src.BindingLayouts.Add(set);
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
            spAddCodeGenTarget(compileRequest, SLANG_GLSL);

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
            auto typeReflection = shaderReflection->findTypeByName(("ParameterBlock<" + TypeName + " >").Buffer());
            if (!typeReflection)
            {
                Print("Type %s not found in compiled shader library %s.\n", TypeName.Buffer(), fileName.Buffer());
                spDestroyCompileRequest(compileRequest);
                return nullptr;
            }
            auto paramBlockLayout = shaderReflection->getTypeLayout(typeReflection)->getElementVarLayout();
            auto typeLayout = paramBlockLayout->getTypeLayout();
            int offset = (int)paramBlockLayout->getOffset(slang::DescriptorTableSlot);
            for (uint32_t i = 0u; i < typeLayout->getFieldCount(); i++)
            {
                ShaderVariableLayout varLayout;
                auto field = typeLayout->getFieldByIndex(i); 
                varLayout.Name = field->getName();
                // We currently do not support resource-typed fields in parameter blocks.
                // Assuming all struct-typed fields as ordinary data
                if (field->getCategory() == slang::Uniform || field->getType()->getKind() == slang::TypeReflection::Kind::Struct)
                {
                    varLayout.Type = ShaderVariableType::Data;
                    varLayout.BindingOffset = (int)field->getOffset();
                    varLayout.BindingLength = (int)field->getTypeLayout()->getSize();
                    varLayout.BindingSpace = 0;
                }
                else
                {
                    auto kind = field->getType()->getKind();
                    if (kind == slang::TypeReflection::Kind::None)
                        continue;
                    if (kind == slang::TypeReflection::Kind::Array)
                    {
                        kind = field->getType()->getElementType()->getKind();
                        varLayout.BindingLength = field->getType()->getElementCount();
                    }
                    else
                        varLayout.BindingLength = 1;
                    auto bindingType = SlangResourceKindToDescriptorType(kind, field->getType()->getResourceShape());
                    switch (bindingType)
                    {
                    case BindingType::UniformBuffer:
                        varLayout.Type = ShaderVariableType::UniformBuffer;
                        break;
                    case BindingType::Sampler:
                        varLayout.Type = ShaderVariableType::Sampler;
                        break;
                    case BindingType::Texture:
                        varLayout.Type = ShaderVariableType::Texture;
                        break;
                    case BindingType::StorageBuffer:
                        varLayout.Type = ShaderVariableType::StorageBuffer;
                        break;
                    default:
                        throw InvalidProgramException("Unknown binding type.");
                    }
                    varLayout.BindingOffset = field->getBindingIndex() + offset;
                    varLayout.BindingSpace = field->getBindingSpace();
                }
                sym->VarLayouts.Add(varLayout.Name, varLayout);
            }
            sym->UniformBufferSize = (int)typeLayout->getSize();
            unsigned int attribCount = typeLayout->getType()->getUserAttributeCount();
            for (unsigned int i = 0; i < attribCount; i++)
            {
                auto attrib = typeLayout->getType()->getUserAttributeByIndex(i);
                ShaderAttribute sa;
                sa.Name = attrib->getName();
                sym->Attributes.Add(sa.Name, sa);
            }
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
        return new SlangShaderCompiler();
    }

    ShaderSet CompileGraphicsShader(ShaderCompilationResult & crs, HardwareRenderer * hw, String fileName)
    {
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