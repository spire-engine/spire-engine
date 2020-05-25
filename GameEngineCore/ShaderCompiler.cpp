#include "ShaderCompiler.h"
#include "CoreLib/LibIO.h"
#include "Engine.h"
#include "ExternalLibs/Slang/slang.h"
#include "CoreLib/Tokenizer.h"

namespace GameEngine
{
	using namespace CoreLib;
	using namespace CoreLib::IO;

    class ShaderCache
    {
    private:
        String path;
        TargetShadingLanguage language;
        EnumerableDictionary<String, int> shaderCodeIndex;
        EnumerableDictionary<int, RefPtr<List<char>>> codeRepo;
        EnumerableDictionary<int, List<DescriptorSetInfo>> bindingLayouts;
        IntSet updatedCodeIndices;
        String GetCacheIndexFileName()
        {
            switch (language)
            {
            case TargetShadingLanguage::HLSL:
                return Path::Combine(path, "index_hlsl.txt");
            case TargetShadingLanguage::SPIRV:
                return Path::Combine(path, "index_spv.txt");
            default:
                return Path::Combine(path, "index.txt");
            }
        }
        String GetShaderCodeFileName(int shaderIndex)
        {
            switch (language)
            {
            case TargetShadingLanguage::HLSL:
                return Path::Combine(path, String("shader_") + String(shaderIndex) + ".hlsl");
            case TargetShadingLanguage::SPIRV:
                return Path::Combine(path, String("shader_") + String(shaderIndex) + ".spv");
            default:
                return Path::Combine(path, String("shader_") + String(shaderIndex) + ".bin");
            }
        }
        String GetBindingLayoutFileName(int shaderIndex)
        {
            return Path::Combine(path, String("shader_") + String(shaderIndex) + ".binding");
        }

        void ReadBindingLayout(List<DescriptorSetInfo> & layout, String fileName)
        {
            layout.Clear();
            BinaryReader reader(new FileStream(fileName));
            int count = reader.ReadInt32();
            layout.SetSize(count);
            for (int i = 0; i < count; i++)
            {
                DescriptorSetInfo& info = layout[i];
                info.BindingPoint = reader.ReadInt32();
                info.Name = reader.ReadString();
                int descCount = reader.ReadInt32();
                info.Descriptors.SetSize(descCount);
                for (int j = 0; j < descCount; j++)
                {
                    info.Descriptors[j].Name = reader.ReadString();
                    info.Descriptors[j].Type = (BindingType)reader.ReadInt32();
                    info.Descriptors[j].Location = reader.ReadInt32();
                    info.Descriptors[j].ArraySize = reader.ReadInt32();
                    info.Descriptors[j].Stages = (StageFlags)reader.ReadInt32();
                }
            }
        }
        void WriteBindingLayout(String fileName, List<DescriptorSetInfo> & layout)
        {
            BinaryWriter writer(new FileStream(fileName, FileMode::Create));
            writer.Write(layout.Count());
            for (int i = 0; i < layout.Count(); i++)
            {
                auto & info = layout[i];
                writer.Write((int32_t)info.BindingPoint);
                writer.Write(info.Name);
                writer.Write(info.Descriptors.Count());
                for (int j = 0; j < info.Descriptors.Count(); j++)
                {
                    writer.Write(info.Descriptors[j].Name);
                    writer.Write((int32_t)info.Descriptors[j].Type);
                    writer.Write(info.Descriptors[j].Location);
                    writer.Write(info.Descriptors[j].ArraySize);
                    writer.Write((int32_t)info.Descriptors[j].Stages);
                }
            }
        }
    public:
        void Load(String cachePath, TargetShadingLanguage lang)
        {
            language = lang;
            path = cachePath;
            if (File::Exists(GetCacheIndexFileName()))
            {
                CoreLib::Text::TokenReader reader(File::ReadAllText(GetCacheIndexFileName()));
                while (!reader.IsEnd())
                {
                    auto key = reader.ReadStringLiteral();
                    auto value = reader.ReadInt();
                    shaderCodeIndex[key] = value;
                }
            }
        }
        void UpdateEntry(String key, List<char> & code, char* glslSrc, List<DescriptorSetInfo>& layouts)
        {
            int value = shaderCodeIndex.Count();
            shaderCodeIndex.TryGetValue(key, value);
            shaderCodeIndex[key] = value;
            codeRepo[value] = new List<char>(code);
            bindingLayouts[value] = layouts;
            updatedCodeIndices.Add(value);
            if (glslSrc)
            {
                File::WriteAllText(Path::ReplaceExt(GetShaderCodeFileName(value), "glsl"), glslSrc);
            }
        }
        bool TryGetEntry(String key, List<char> & code, List<DescriptorSetInfo>& layouts)
        {
            int value = -1;
            if (shaderCodeIndex.TryGetValue(key, value))
            {
                RefPtr<List<char>> srcCode;
                if (!codeRepo.TryGetValue(value, srcCode))
                {
                    auto shaderFile = GetShaderCodeFileName(value);
                    if (File::Exists(shaderFile))
                    {
                        auto bytes = File::ReadAllBytes(shaderFile);
                        srcCode = new List<char>();
                        srcCode->AddRange((char*)bytes.Buffer(), bytes.Count());
                        codeRepo[value] = srcCode;
                    }
                    else
                    {
                        shaderCodeIndex.Remove(key);
                        return false;
                    }
                }
                if (!bindingLayouts.TryGetValue(value, layouts))
                {
                    auto bindingFile = GetBindingLayoutFileName(value);
                    if (File::Exists(bindingFile))
                    {
                        ReadBindingLayout(layouts, bindingFile);
                    }
                    else
                    {
                        shaderCodeIndex.Remove(key);
                        return false;
                    }
                }
                code = *srcCode;
                return true;
            }
            return false;
        }
        void Save()
        {
            for (auto & code : codeRepo)
            {
                if (updatedCodeIndices.Contains(code.Key))
                {
                    auto fileName = GetShaderCodeFileName(code.Key);
                    File::WriteAllBytes(fileName, code.Value->Buffer(), code.Value->Count());
                }
            }
            for (auto & binding : bindingLayouts)
            {
                if (updatedCodeIndices.Contains(binding.Key))
                {
                    auto fileName = GetBindingLayoutFileName(binding.Key);
                    WriteBindingLayout(fileName, binding.Value);
                }
            }
            StreamWriter writer(GetCacheIndexFileName());
            for (auto & idx : shaderCodeIndex)
            {
                writer << CoreLib::Text::EscapeStringLiteral(idx.Key) << " " << idx.Value << "\n";
            }
        }
    };

    class SlangShaderCompiler : public IShaderCompiler
    {
    public:
        bool dumpShaderSource = true;
        EnumerableDictionary<String, SlangCompileRequest*> reflectionCompileRequests;
        EnumerableDictionary<String, RefPtr<ShaderEntryPoint>> shaderEntryPoints;
        EnumerableDictionary<String, RefPtr<ShaderTypeSymbol>> shaderTypeSymbols;
        SlangSession *session = nullptr;
        StringBuilder sb;
        ShaderCache cache;
        SlangShaderCompiler()
        {
            cache.Load(Engine::Instance()->GetDirectory(false, ResourceType::ShaderCache), Engine::Instance()->GetTargetShadingLanguage());
        }
        ~SlangShaderCompiler()
        {
            cache.Save();
            for (auto cr : reflectionCompileRequests)
                spDestroyCompileRequest(cr.Value);
            if (session)
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
            default:
                throw ArgumentException("Unknown shader type.");
            }
        }
        BindingType SlangResourceKindToDescriptorType(slang::TypeReflection::Kind k, SlangResourceShape shape, SlangResourceAccess access)
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
                    if (access == SLANG_RESOURCE_ACCESS_READ_WRITE)
                        return BindingType::StorageTexture;
                    else
                        return BindingType::Texture;
                }
            default:
                throw ArgumentException("Unknown slang resource kind.");
            }
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
            StringBuilder sbKey;
            List<String> keys;
            src.ShaderCode.SetSize(entryPoints.Count());
            List<int> entryPointsToCompile;
            for (int i = 0; i < entryPoints.Count(); i++)
            {
                auto entryPoint = entryPoints[i];
                sbKey.Clear();
                sbKey << entryPoint->FileName << "|" << entryPoint->FunctionName;
                if (env)
                {
                    for (auto & t : env->SpecializationTypes)
                    {
                        sbKey << "|" << t->TypeName;
                    }
                }
                keys.Add(sbKey.ToString());
                List<char> code;
                if (!cache.TryGetEntry(keys.Last(), src.ShaderCode[i], src.BindingLayouts))
                {
                    entryPointsToCompile.Add(i);
                }
            }
            if (entryPointsToCompile.Count())
            {
                StageFlags stageFlags = sfNone;
                auto req = NewCompileRequest();
                Dictionary<String, int> addedTUs;
                for (auto eid : entryPointsToCompile)
                {
                    auto ep = entryPoints[eid];
                    int unit = 0;
                    auto path = Engine::Instance()->FindFile(ep->FileName, ResourceType::Shader);
                    if (path.Length() == 0)
                        throw IOException(String("Shader file not found: ") + ep->FileName + String("\nDid you forget to specify '-enginedir'?"));
                    if (!addedTUs.TryGetValue(path, unit))
                    {
                        unit = spAddTranslationUnit(req, SLANG_SOURCE_LANGUAGE_SLANG, ep->FileName.Buffer());
                        spAddTranslationUnitSourceFile(req, unit, path.Buffer());
                        addedTUs[path] = unit;
                    }
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

                List<char*> glslOutput;

                for (int i = 0; i < entryPointsToCompile.Count(); i++)
                {
                    ISlangBlob* outBlob = nullptr;
                    spGetEntryPointCodeBlob(req, i, 0, &outBlob);
                    int size = (int)outBlob->getBufferSize();
                    src.ShaderCode[entryPointsToCompile[i]].SetSize(size);
                    memcpy(src.ShaderCode[entryPointsToCompile[i]].Buffer(), outBlob->getBufferPointer(), size);
                    if (dumpShaderSource)
                    {
                        spGetEntryPointCodeBlob(req, i, 1, &outBlob);
                        glslOutput.Add((char*)outBlob->getBufferPointer());
                    }
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
                                desc.Type = SlangResourceKindToDescriptorType(field->getType()->getElementType()->getKind(), field->getType()->getResourceShape(), field->getType()->getResourceAccess());
                                desc.ArraySize = (int)field->getType()->getElementCount();
                            }
                            else
                            {
                                desc.Type = SlangResourceKindToDescriptorType(field->getType()->getKind(), field->getType()->getResourceShape(), field->getType()->getResourceAccess());
                            }
                            desc.Stages = stageFlags;
                            set.Descriptors.Add(desc);
                        }
                        src.BindingLayouts.Add(set);
                    }
                }

                for (int i = 0; i < entryPointsToCompile.Count(); i++)
                {
                    auto eid = entryPointsToCompile[i];
                    char* glsl = nullptr;
                    if (dumpShaderSource)
                        glsl = glslOutput[i];
                    cache.UpdateEntry(keys[eid], src.ShaderCode[eid], glsl, src.BindingLayouts);
                }

                spDestroyCompileRequest(req);
            }
            return true;
        }
        virtual ShaderTypeSymbol* LoadSystemTypeSymbol(CoreLib::String TypeName) override
        {
            return LoadTypeSymbol("ShaderLib.slang", TypeName);
        }
        SlangCompileRequest* NewCompileRequest()
        {
            if (!session)
                session = spCreateSession();

            auto compileRequest = spCreateCompileRequest(session);
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(true, ResourceType::Shader).Buffer());
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(false, ResourceType::Shader).Buffer());
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(true, ResourceType::Material).Buffer());
            spAddSearchPath(compileRequest, Engine::Instance()->GetDirectory(false, ResourceType::Material).Buffer());

            spAddCodeGenTarget(compileRequest, GetSlangTarget());
            spSetTargetProfile(compileRequest, 0, spFindProfile(session, "sm_5_1"));
            if (dumpShaderSource)
            {
                if (GetSlangTarget() == SLANG_DXBC)
                    spAddCodeGenTarget(compileRequest, SLANG_HLSL);
                else
                    spAddCodeGenTarget(compileRequest, SLANG_GLSL);
            }

            #if 0
                spSetDumpIntermediates(compileRequest, 1);
            #endif
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
            SlangCompileRequest* compileRequest = nullptr;
            if (!reflectionCompileRequests.TryGetValue(fileName, compileRequest))
            {
                compileRequest = NewCompileRequest();
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
                reflectionCompileRequests[fileName] = compileRequest;
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
                // We currently do not support resource-typed fields in struct-typed parameter block fields.
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
                        varLayout.BindingLength = (int)field->getType()->getElementCount();
                    }
                    else
                        varLayout.BindingLength = 1;
                    auto bindingType = SlangResourceKindToDescriptorType(kind, field->getType()->getResourceShape(), field->getType()->getResourceAccess());
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