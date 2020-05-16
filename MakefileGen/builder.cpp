#include "../CoreLib/Tokenizer.h"
#include "../CoreLib/LibIO.h"
#include "../CoreLib/LibMath.h"
#include <dirent.h>

using namespace CoreLib;

struct FileTarget
{
    bool analyzed = false;
    bool isHeader = true;
    String fileName;
    List<String> dependentFiles;
    String dependencyName;
    String dirDependency;
    int dirId = 0;
};

String EscapeStr(String str)
{
    StringBuilder rs;
    for (int i = 0; i < str.Length(); i++)
    {
        auto c = str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z'))
        {
            rs << c;
        }
        else
        {
            rs << "_";
        }
    }
    return rs.ToString();
}

void AnalyzeDependency(FileTarget& target, EnumerableDictionary<String, List<FileTarget*>> & shortNameMapping, StringBuilder &sb)
{
    static int depCounter = 0;
    if (target.analyzed) return;
    target.analyzed = true;
    auto text = CoreLib::IO::File::ReadAllText(target.fileName);
    int ptr = 0;
    while (ptr < text.Length())
    {
        ptr = text.IndexOf("#include", ptr);
        if (ptr == -1) break;
        int lineEndPos = text.IndexOf('\n', ptr);
        if (lineEndPos == -1) lineEndPos = text.Length();
        auto includeName = (text.SubString(ptr+9, lineEndPos-(ptr+9))).Trim();
        includeName = includeName.SubString(1, includeName.Length() - 2);
        auto includeFileName = IO::Path::GetFileName(includeName);
        ptr = lineEndPos;
        // Try to see the included file is one of the source files.
        // We perform a match by fileName only, without considering path.
        // Therefore the match result can be ambiguous, but this is OK.
        // It is fine to be conservative and add all matches as its dependency.
        if (auto dependentFiles = shortNameMapping.TryGetValue(includeFileName))
        {
            for (auto & f : *dependentFiles)
            {
                if (!f->analyzed)
                    AnalyzeDependency(*f, shortNameMapping, sb);
                if (f->dependencyName.Length())
                    target.dependentFiles.Add("$(" + f->dependencyName + ")");
            }
        }
    }
    depCounter++;
    target.dependencyName = "DEP_" + String(depCounter);
    sb << target.dependencyName << " := " << target.fileName;
    for (auto & dep : target.dependentFiles)
        sb << " " << dep;
    sb << "\n";
}

int main(int argc, const char* argv[])
{
    if (argc != 2) return 0;
    auto projectFile = argv[1];
    String projectFileContent = CoreLib::IO::File::ReadAllText(projectFile);
    CoreLib::Text::TokenReader tokenReader(projectFileContent);
    auto projectName = tokenReader.ReadWord();
    List<String> blobs;
    
    StringBuilder makefileSb;
    makefileSb << R"(
PLATFORM := $(shell uname -s | tr '[:upper:]' '[:lower:]')
ARCHITECTURE := $(shell uname -p)

ifeq (,$(CONFIGURATION))
	CONFIGURATION := release
endif

TARGET := $(PLATFORM)-$(ARCHITECTURE)

OUTPUTDIR := build/$(TARGET)/$(CONFIGURATION)/
INTERMEDIATEDIR := build/intermediate/$(TARGET)/$(CONFIGURATION)/

LDFLAGS := -L$(OUTPUTDIR)

COMPILER_NAME := $(notdir $(CXX))

ifeq (debug,$(CONFIGURATION))
CFLAGS += -g -D_DEBUG
else
CFLAGS += -O2
endif

RELATIVE_RPATH_INCANTATION := "-Wl,-rpath,"'$$'"ORIGIN/"
)";

    tokenReader.Read("{");
    while (!tokenReader.IsEnd() && !tokenReader.LookAhead("}"))
    {
        auto head = tokenReader.ReadToken();
        if (head.Content == "cppblob")
        {
            blobs.Add(tokenReader.ReadStringLiteral());            
        }
        else if (head.Content == "link")
        {
            auto linkOption = tokenReader.ReadStringLiteral();
            if (!linkOption.StartsWith('-'))
                makefileSb << "LDFLAGS += -l" << linkOption << "\n";
            else
                makefileSb << "LDFLAGS += " << linkOption << "\n";
        }
        else if (head.Content == "link_debug")
        {
            makefileSb << "ifeq (debug,$(CONFIGURATION))\n\t";
            auto linkOption = tokenReader.ReadStringLiteral();
            if (!linkOption.StartsWith('-'))
                makefileSb << "LDFLAGS += -l" << linkOption << "\n";
            else
                makefileSb << "LDFLAGS += " << linkOption<< "\n";
            makefileSb << "endif\n";
        }
        else if (head.Content == "link_release")
        {
            makefileSb << "ifeq (release,$(CONFIGURATION))\n\t";
            auto linkOption = tokenReader.ReadStringLiteral();
            if (!linkOption.StartsWith('-'))
                makefileSb << "LDFLAGS += -l" << linkOption << "\n";
            else
                makefileSb << "LDFLAGS += " << linkOption << "\n";
            makefileSb << "endif\n";
        }
        else if (head.Content == "libdir")
        {
            makefileSb << "LDFLAGS += -L" << tokenReader.ReadStringLiteral() << "\n";
        }
        else if (head.Content == "include")
        {
            makefileSb << "CFLAGS += -I" << tokenReader.ReadStringLiteral() << "\n";
        }
        else if (head.Content == "cflags")
        {
            makefileSb << "CFLAGS += " << tokenReader.ReadStringLiteral() << "\n";
        }
        else if (head.Content == "cflags_release")
        {
            makefileSb << "ifeq (release,$(CONFIGURATION))\n\tCFLAGS += " 
                << tokenReader.ReadStringLiteral() << "\nendif\n";
        }
        else if (head.Content == "cflags_debug")
        {
            makefileSb << "ifeq (debug,$(CONFIGURATION))\n\tCFLAGS += " 
                << tokenReader.ReadStringLiteral() << "\nendif\n";
        }
        else if (head.Content == "cflags_gcc")
        {
            makefileSb << "ifeq (g++,$(COMPILER_NAME))\n\tCFLAGS += "; 
            makefileSb << tokenReader.ReadStringLiteral();
            makefileSb << "\nendif\n";
        }
        else if (head.Content == "cflags_clang")
        {
            makefileSb << "ifeq (clang,$(COMPILER_NAME))\n\tCFLAGS += "; 
            makefileSb << tokenReader.ReadStringLiteral();
            makefileSb << "\nendif\n";
        }
        else if (head.Content == "}")
        {
            break;
        }
    }

    // Generate make file
    
    EnumerableDictionary<String, RefPtr<FileTarget>> sourceFiles;
    EnumerableDictionary<String, List<FileTarget*>> shortNameMapping;
    int dirId = 0;
    for (auto blob : blobs)
    {
        DIR* dir;
        struct dirent* ent;
        if ((dir = opendir(blob.Buffer())) != NULL)
        {
            auto dirDepName = "dir_" + String(dirId);
            auto buildTargetName = "TARGET_" + EscapeStr(blob);
            makefileSb << "BD_" << dirId << " := " << "$(INTERMEDIATEDIR)" << blob << "\n";
            makefileSb << dirDepName << ":\n\t@mkdir -p $(BD_" << dirId << ")\n";
            makefileSb << buildTargetName << ": ";
            while ((ent = readdir(dir)) != NULL)
            {
                RefPtr<FileTarget> target = new FileTarget();
                target->fileName = blob + ent->d_name;
                target->dirDependency = dirDepName;
                target->dirId = dirId;
                auto shortName = ent->d_name;
                if (target->fileName.EndsWith(".cpp") || target->fileName.EndsWith(".c"))
                {
                    makefileSb << " $(BD_" << dirId << ")" << IO::Path::GetFileNameWithoutEXT(target->fileName) + ".o";
                    target->isHeader = false;
                }
                else if (target->fileName.EndsWith(".h") || target->fileName.EndsWith(".hpp") || target->fileName.EndsWith(".inc"))
                {
                    target->isHeader = true;
                }
                else
                {
                    continue;
                }
                sourceFiles[target->fileName] = target;
                auto mappingList = shortNameMapping.TryGetValue(shortName);
                if (!mappingList)
                {
                    shortNameMapping[shortName] = List<FileTarget*>();
                    mappingList = shortNameMapping.TryGetValue(shortName);
                }
                mappingList->Add(target.Ptr());
            }
            dirId++;
            makefileSb << "\n";
            closedir (dir);
        }
    }

    makefileSb << "\n";

    for (auto & source : sourceFiles)
    {
        AnalyzeDependency(*source.Value, shortNameMapping, makefileSb);
    }

    for (auto & source : sourceFiles)
    {
        if (!source.Value->isHeader)
        {
            auto targetName = "$(BD_" + String(source.Value->dirId) + ")" + 
                IO::Path::GetFileNameWithoutEXT(source.Key) + ".o";
            makefileSb << targetName << ": " << source.Key;
            for (auto dep : source.Value->dependentFiles)
                makefileSb << " " << dep;
            makefileSb << " | " << source.Value->dirDependency << "\n";
            makefileSb << "\n\t@echo \"$(COMPILER_NAME): " << source.Key << "\"\n";
            makefileSb << "\n\t@$(CXX) -c " << source.Key << " -o " << targetName << " $(CFLAGS)" << "\n";
        }
    }

    // .PHONY
    makefileSb << ".PHONY: ";
    for (auto blob : blobs)
    {
        makefileSb << " TARGET_" << EscapeStr(blob);
    }
    makefileSb << "\n";

    // Generate linking
    makefileSb << projectName << ": $(OUTPUTDIR)libslang-glslang.so $(OUTPUTDIR)libslang.so";
    for (auto blob : blobs)
    {
        makefileSb << " TARGET_" << EscapeStr(blob);
    }
    makefileSb << "| $(OUTPUTDIR)\n";
    makefileSb << "\t@$(CXX) -o $(OUTPUTDIR)" << projectName << " ";
    for (auto blob : blobs)
    {
        makefileSb << " $(INTERMEDIATEDIR)" << blob << "*.o";
    }
    makefileSb << " $(LDFLAGS) -ldl $(RELATIVE_RPATH_INCANTATION) -pthread\n";
    makefileSb << "\t@echo \"Binary compiled at: $(OUTPUTDIR)" + projectName + "\"\n";
    makefileSb << R"(
$(OUTPUTDIR):
	@mkdir -p $(OUTPUTDIR)
$(INTERMEDIATEDIR):
	@mkdir -p $(INTERMEDIATEDIR)

$(OUTPUTDIR)libslang-glslang.so: | $(OUTPUTDIR)
	cp ExternalLibs/Slang/bin/linux-x64/release/libslang-glslang.so $@
$(OUTPUTDIR)libslang.so: | $(OUTPUTDIR)
	cp ExternalLibs/Slang/bin/linux-x64/release/libslang.so $@
)";

    IO::File::WriteAllText("makefile-gen", makefileSb.ToString());
    return 0;
}