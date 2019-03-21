#include "TextureFile.h"

namespace CoreLib
{
	namespace Graphics
	{
		using namespace CoreLib::Basic;
		using namespace CoreLib::IO;

		TextureFile::TextureFile(String fileName)
		{
			FileStream stream(fileName);
			LoadFromStream(&stream);
		}
		TextureFile::TextureFile(Stream * stream)
		{
			LoadFromStream(stream);
		}
		double GetPixelSize(TextureStorageFormat format)
		{
			switch (format)
			{
			case TextureStorageFormat::R8:
				return 1;
			case TextureStorageFormat::RG8:
				return 2;
			case TextureStorageFormat::RGB8:
				return 3;
			case TextureStorageFormat::RGBA8:
				return 4;
			case TextureStorageFormat::R_F32:
				return 4;
			case TextureStorageFormat::RG_F32:
				return 8;
			case TextureStorageFormat::RGB_F32:
				return 12;
			case TextureStorageFormat::RGBA_F32:
				return 16;
			case TextureStorageFormat::BC1:
				return 0.5f;
			case TextureStorageFormat::BC3:
			case TextureStorageFormat::BC5:
				return 1;
			default:
				return 0;
			}
		}
		void TextureFile::LoadFromStream(Stream * stream)
		{
            bool error = false;
			BinaryReader reader(stream);
			TextureFileHeader header;
			int headerSize = reader.ReadInt32();
			reader.Read((unsigned char*)&header, headerSize);
            type = header.Type;
			if (header.Type == TextureType::Texture2D)
			{
				width = header.Width;
				height = header.Height;
				format = header.Format;
				mipLevels = reader.ReadInt32();
                Allocate(format, width, height, mipLevels, 1);
                size_t offset = 0;
                for (int i = 0; i < mipLevels; i++)
                {
                    int bufSize = reader.ReadInt32();
                    reader.Read(buffer.Buffer() + offset, bufSize);
                    offset += bufSize;
                    if (bufSize != GetImagePlaneSize(Math::Max(1, width >> i), Math::Max(1, height >> i)))
                    {
                        error = true;
                        goto end;
                    }
				}
			}
        end:;
			reader.ReleaseStream();
            if (error)
                throw IOException("Invalid texture content.");
		}
		void TextureFile::SaveToStream(Stream * stream)
		{
			BinaryWriter writer(stream);
			int headerSize = sizeof(TextureFileHeader);
			writer.Write(headerSize);
			TextureFileHeader header;
			header.Format = format;
			header.Width = width;
			header.Height = height;
            header.ArrayLength = arrayLength;
			header.Type = type;
			writer.Write(header);
			writer.Write(mipLevels);
            size_t offset = 0;
			for (int i = 0; i < mipLevels; i++)
			{
                size_t size = GetImagePlaneSize(width >> i, height >> i);
				writer.Write((int)size);
				writer.Write(buffer.Buffer() + offset);
                offset += size;
			}
			writer.ReleaseStream();
		}
        void TextureFile::Allocate(TextureStorageFormat storageFormat, int w, int h, int levels, int arrayCount)
        {
            if (arrayCount <= 1)
                type = TextureType::Texture2D;
            else
                type = TextureType::Texture2DArray;
            format = storageFormat;
            width = w;
            height = h;
            levels = levels;
            arrayLength = arrayCount;
            auto size = GetArrayStride() * (size_t)arrayCount;
            buffer.SetSize((int)size);
        }
		void TextureFile::SaveToFile(String fileName)
		{
			FileStream stream(fileName, FileMode::Create);
			SaveToStream(&stream);
		}

        size_t GetTextureDataSize(TextureStorageFormat format, int width, int height)
        {
            size_t res = (size_t)(width) * (size_t)height;
            size_t blocks = (((size_t)width + 3) >> 2) * (((size_t)height + 3) >> 2);
            switch (format)
            {
            case TextureStorageFormat::R8:
                return res;
            case TextureStorageFormat::RG8:
                return res * 2;
            case TextureStorageFormat::RGB8:
                return res * 3;
            case TextureStorageFormat::RGBA8:
            case TextureStorageFormat::R_F32:
                return res * 4;
            case TextureStorageFormat::RG_F32:
                return res * 8;
            case TextureStorageFormat::RGB_F32:
                return res * 12;
            case TextureStorageFormat::RGBA_F32:
                return res * 16;
            case TextureStorageFormat::BC1:
                return blocks * 8;
            case TextureStorageFormat::BC3:
            case TextureStorageFormat::BC5:
            case TextureStorageFormat::BC6H:
            case TextureStorageFormat::BC7:
                return blocks * 16;
            default:
                return 0;
            }
        }

        CoreLib::List<char> TranslateThreeChannelTextureFormat(char * buffer, int pixelCount, int channelSize)
		{
			CoreLib::List<char> result;
			result.SetSize(pixelCount * channelSize * 4);
			char * dest = result.Buffer();
			char * src = buffer;
			for (int i = 0; i < pixelCount; i++)
			{
				for (int j = 0; j < channelSize * 3; j++)
					dest[i*channelSize * 4 + j] = src[i*channelSize * 3 + j];
				for (int j = 0; j < channelSize; j++)
					dest[(i * 4 + 3)*channelSize + j] = 0;
			}
			return _Move(result);
		}
	}
}