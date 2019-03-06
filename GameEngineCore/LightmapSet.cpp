#include "LightmapSet.h"
#include "Level.h"
#include "CoreLib/LibIO.h"
#include "Engine.h"

using namespace CoreLib;
using namespace CoreLib::IO;

namespace GameEngine
{
    enum class LightmapType
    {
        Simple
    };
    const int LightmapSetFileVersionMajor = 0;
    const int LightmapSetFileVersionMinor = 1;
    const int LightmapSetFileVersion = (LightmapSetFileVersionMajor << 16) + LightmapSetFileVersionMinor;
    struct LightmapSetFileHeader
    {
        char Identifier[4] = {'G', 'L', 'M', 'S'};
        int Version = LightmapSetFileVersion;
        int LightmapCount = 0;
        int ActorIndexCount = 0;
        LightmapType Type;
        int Reserved[16] = {};
    };

    void LightmapSet::SaveToFile(Level * /*level*/, CoreLib::String fileName)
    {
        LightmapSetFileHeader header;
        header.LightmapCount = Lightmaps.Count();
        header.ActorIndexCount = ActorLightmapIds.Count();
        BinaryWriter writer(new FileStream(fileName, FileMode::Create));
        writer.Write(header);
        for (auto & element : ActorLightmapIds)
        {
            writer.Write(element.Key->Name.GetValue());
            writer.Write(element.Value);
        }
        for (int i = 0; i < Lightmaps.Count(); i++)
        {
            Lightmaps[i].SaveToStream(writer);
        }
    }

    void LightmapSet::LoadFromFile(Level * level, CoreLib::String fileName)
    {
        BinaryReader reader(new FileStream(fileName, FileMode::Open));
        LightmapSetFileHeader header;
        reader.Read(header);
        if (strncmp(header.Identifier, "GLMS", 4) != 0)
            throw IO::IOException("Invalid lightmap file.");
        for (int i = 0; i < header.ActorIndexCount; i++)
        {
            auto actorName = reader.ReadString();
            auto lightmapId = reader.ReadInt32();
            auto actor = level->FindActor(actorName);
            if (actor)
            {
                ActorLightmapIds[actor] = lightmapId;
            }
            else
            {
                Engine::Print("Warning: lightmap set '%S' defines lightmap for actor '%S', which no longer exists in level '%S'. \n",
                    fileName.ToWString(), actorName.ToWString(), level->FileName.ToWString());
            }
        }
        Lightmaps.SetSize(header.LightmapCount);
        for (int i = 0; i < Lightmaps.Count(); i++)
        {
            Lightmaps[i].LoadFromStream(reader);
        }
    }

}