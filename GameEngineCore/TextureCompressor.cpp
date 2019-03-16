#include "TextureCompressor.h"
#define STB_DXT_IMPLEMENTATION
#include "TextureTool/stb_dxt.h"
#include "TextureTool/LibSquish.h"

namespace GameEngine
{
	using namespace CoreLib;
	using namespace CoreLib::Graphics;

	CoreLib::List<unsigned char> Resample(const CoreLib::List<unsigned char> &rgbaPixels, int w, int h, int & nw, int & nh)
	{
		nw = w / 2;
		nh = h / 2;
		if (nw < 1)
			nw = 1;
		if (nh < 1)
			nh = 1;
		CoreLib::List<unsigned char> rs;
		rs.SetSize(nh * nw * 4);
		for (int i = 0; i < nh; i++)
		{
			int i0 = Math::Clamp(i * 2, 0, h - 1);
			int i1 = Math::Clamp(i * 2 + 1, 0, h - 1);
			for (int j = 0; j < nw; j++)
			{
				int j0 = Math::Clamp(j * 2, 0, w - 1);
				int j1 = Math::Clamp(j * 2 + 1, 0, w - 1);
				for (int k = 0; k < 4; k++)
					rs[(i * nw + j) * 4 + k] = (rgbaPixels[(i0 * w + j0) * 4 + k] 
						+ rgbaPixels[(i0 * w + j1) * 4 + k] 
						+ rgbaPixels[(i1 * w + j0) * 4 + k] 
						+ rgbaPixels[(i1 * w + j1) * 4 + k]) / 4;
			}
		}
		return rs;
	}

    template<typename TBlockCompressFunc>
    void CompressTexture(TextureFile & result, TextureStorageFormat format, const TBlockCompressFunc & compressFunc, const CoreLib::ArrayView<unsigned char> & rgbaPixels, int width, int height)
    {
        int blockSize = 0;
        switch (format)
        {
        case TextureStorageFormat::BC1:
            blockSize = 8;
            break;
        default:
            blockSize = 16;
        }
        List<unsigned char> input;
        input.AddRange(rgbaPixels.Buffer(), rgbaPixels.Count());
        int w = width;
        int h = height;
        int level = 0;
        result.Allocate(format, width, height, Math::Max(Math::Log2Ceil(width), Math::Log2Ceil(height)) + 1, 1);
        while (w >= 1 || h >= 1)
        {
            auto buffer = result.GetBuffer(level);
            int ptr = 0;
            #pragma omp parallel for
            for (int i = 0; i < h; i += 4)
            {
                for (int j = 0; j < w; j += 4)
                {
                    unsigned char block[64], outBlock[8];
                    for (int ki = 0; ki < 4; ki++)
                    {
                        int ni = Math::Clamp(i + ki, 0, h - 1);
                        for (int kj = 0; kj < 4; kj++)
                        {
                            int nj = Math::Clamp(j + kj, 0, w - 1);
                            for (int c = 0; c < 4; c++)
                                block[(ki * 4 + kj) * 4 + c] = input[(ni * h + nj) * 4 + c];
                        }
                    }
                    compressFunc(outBlock, block);
                    memcpy(buffer.Buffer() + ptr, outBlock, 8);
                    ptr += blockSize;
                }
            }

            if (w == 1 && h == 1) break;
            int nw, nh;
            input = Resample(input, w, h, nw, nh);
            w = nw;
            h = nh;
            level++;
        }
    }

	void TextureCompressor::CompressRGBA_BC1(TextureFile & result, const CoreLib::ArrayView<unsigned char> & rgbaPixels, int width, int height)
	{
        CompressTexture(result, TextureStorageFormat::BC1, [](unsigned char* output, unsigned char* input) {stb_compress_dxt_block(output, input, 0, STB_DXT_HIGHQUAL); },
            rgbaPixels, width, height);
	}

	void TextureCompressor::CompressRGBA_BC3(TextureFile & result, const CoreLib::ArrayView<unsigned char> & rgbaPixels, int width, int height)
	{
        CompressTexture(result, TextureStorageFormat::BC3, [](unsigned char* output, unsigned char* input) {stb_compress_dxt_block(output, input, 1, STB_DXT_HIGHQUAL); },
            rgbaPixels, width, height);
	}

	void TextureCompressor::CompressRG_BC5(TextureFile & result, const CoreLib::ArrayView<unsigned char> & rgbaPixels, int width, int height)
	{
        CompressTexture(result, TextureStorageFormat::BC5, [](unsigned char* output, unsigned char* input)
            {
                stb__CompressAlphaBlock(output, (unsigned char*)input, 4);
                stb__CompressAlphaBlock(output + 8, (unsigned char*)input + 1, 4);
            },
            rgbaPixels, width, height);
	}
}

