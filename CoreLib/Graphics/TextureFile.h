#ifndef CORELIB_GRAPHICS_TEXTURE_FILE_H
#define CORELIB_GRAPHICS_TEXTURE_FILE_H

#include "../Basic.h"
#include "../LibIO.h"

namespace CoreLib
{
	namespace Graphics
	{
        const int TextureType1DBit = 1;
        const int TextureType3DBit = 4;
        const int TextureTypeCubeBit = 8;
        const int TextureTypeArrayBit = 16;
        const int TextureTypeExtendedBit = 1024;
        enum class TextureType : short
        {
            Texture2D,
            Texture1D = TextureType1DBit,
            Texture2DArray = TextureTypeArrayBit | TextureTypeExtendedBit,
            TextureCube = TextureTypeCubeBit | TextureTypeExtendedBit,
            TextureCubeArray = TextureTypeCubeBit | TextureTypeArrayBit | TextureTypeExtendedBit
		};
		enum class TextureStorageFormat : short
		{
			R8, RG8, RGB8, RGBA8,
			R_F32, RG_F32, RGB_F32, RGBA_F32, 
            BC1, BC5, BC3, BC6H, BC7
		};
		class TextureFileHeader
		{
		public:
			TextureType Type;
			TextureStorageFormat Format;
			int Width, Height;
            int ArrayLength = 0;
		};

        size_t GetTextureDataSize(TextureStorageFormat format, int width, int height);

		class TextureFile
		{
		private:
			CoreLib::Basic::List<unsigned char> buffer;
			TextureStorageFormat format;
            TextureType type;
			int width, height, arrayLength = 1;
            int mipLevels;
			void LoadFromStream(CoreLib::IO::Stream * stream);
		public:
			TextureFile()
			{
				width = height = 0;
				format = TextureStorageFormat::RGBA8;
			}
			TextureFile(CoreLib::Basic::String fileName);
			TextureFile(CoreLib::IO::Stream * stream);
			TextureStorageFormat GetFormat()
			{
				return format;
			}
			int GetWidth()
			{
				return width;
			}
			int GetHeight()
			{
				return height;
			}
			int GetMipLevels()
			{
				return mipLevels;
			}
            size_t GetImagePlaneSize(int w, int h)
            {
                return GetTextureDataSize(format, w, h);
            }
            size_t GetArrayStride()
            {
                return GetMipmapLevelOffset(mipLevels);
            }
            size_t GetMipmapLevelOffset(int level)
            {
                size_t size = 0;
                for (int i = 0; i < level; i++)
                {
                    int lw = width >> i;
                    int lh = height >> i;
                    size += GetImagePlaneSize(lw, lh);
                }
                return size;
            }
			void SaveToFile(CoreLib::Basic::String fileName);
			void SaveToStream(CoreLib::IO::Stream * stream);
			//void SetData(TextureStorageFormat format, int width, int height, int level, int arrayIndex, CoreLib::Basic::ArrayView<unsigned char> data);
            void Allocate(TextureStorageFormat format, int width, int height, int levelCount, int arrayCount);
			CoreLib::Basic::ArrayView<unsigned char> GetBuffer(int level = 0, int arrayIndex = 0)
			{
                auto arrayStride = GetArrayStride();
                return CoreLib::Basic::ArrayView<unsigned char>(buffer.Buffer() + arrayStride * arrayIndex + GetMipmapLevelOffset(level), (int)arrayStride);
			}
			CoreLib::Basic::List<float> GetPixels()
			{
				CoreLib::Basic::List<float> pixels;
				pixels.SetSize(width * height * 4);
				for (int i = 0; i < width*height; i++)
				{
					float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					switch (format)
					{
					case TextureStorageFormat::R8:
						color[0] = buffer[i] / 255.0f;
						break;
					case TextureStorageFormat::RG8:
						color[0] = buffer[i*2] / 255.0f;
						color[1] = buffer[i*2 + 1] / 255.0f;
						break;
					case TextureStorageFormat::RGB8:
						color[0] = buffer[i * 3] / 255.0f;
						color[1] = buffer[i * 3 + 1] / 255.0f;
						color[2] = buffer[i * 3 + 2] / 255.0f;
						break;
					case TextureStorageFormat::RGBA8:
						color[0] = buffer[i * 4] / 255.0f;
						color[1] = buffer[i * 4 + 1] / 255.0f;
						color[2] = buffer[i * 4 + 2] / 255.0f;
						color[3] = buffer[i * 4 + 3] / 255.0f;
						break;
					case TextureStorageFormat::R_F32:
						color[0] = ((float*)buffer.Buffer())[i];
						break;
					case TextureStorageFormat::RG_F32:
						color[0] = ((float*)buffer.Buffer())[i*2];
						color[1] = ((float*)buffer.Buffer())[i*2 + 1];
						break;
					case TextureStorageFormat::RGB_F32:
						color[0] = ((float*)buffer.Buffer())[i * 3];
						color[1] = ((float*)buffer.Buffer())[i * 3 + 1];
						color[2] = ((float*)buffer.Buffer())[i * 3 + 2];
						break;
					case TextureStorageFormat::RGBA_F32:
						color[0] = ((float*)buffer.Buffer())[i * 4];
						color[1] = ((float*)buffer.Buffer())[i * 4 + 1];
						color[2] = ((float*)buffer.Buffer())[i * 4 + 2];
						color[3] = ((float*)buffer.Buffer())[i * 4 + 3];
						break;
					default:
						throw NotImplementedException();
					}
					pixels[i * 4] = color[0];
					pixels[i * 4 + 1] = color[1];
					pixels[i * 4 + 2] = color[2];
					pixels[i * 4 + 3] = color[3];
				}
				return pixels;
			}
		};

		CoreLib::List<char> TranslateThreeChannelTextureFormat(char * buffer, int pixelCount, int channelSize);
	}
}

#endif