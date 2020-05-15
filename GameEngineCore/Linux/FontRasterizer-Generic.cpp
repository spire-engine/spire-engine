#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "OS.h"
#include "Engine.h"
#include "CoreLib/LibIO.h"

namespace GameEngine
{
    using namespace GraphicsUI;
    using namespace CoreLib;

    String FindFontFile(const Font& font)
    {
        // Try directly find a font file under game directory named font.FontName.
        auto fontFile = Engine::Instance()->FindFile(font.FontName, ResourceType::Font);
        // If we can find this file, return.
        if (fontFile.Length() != 0)
            return fontFile;
        // Otherwise, return a default font.
        const char* fontFileName = nullptr;
        if (font.FontName == "Webdings" || font.FontName == "UISymbols")
            fontFileName = "UISymbols/uisymbols.ttf";
        else
        {
            if (font.Bold)
                fontFileName = "OpenSans/OpenSans-Bold.ttf";
            else
                fontFileName = "OpenSans/OpenSans-Regular.ttf";
        }
        return Engine::Instance()->FindFile(fontFileName, ResourceType::Font);
    }

    struct GlyphCacheItem
    {
        unsigned codePoint;
        bool isValid = false;
        int x0, x1, y0, y1;
        int advanceWidth;
        int leftSideBearing;
        List<unsigned char> bitmap;
        GlyphCacheItem* next;
        GlyphCacheItem* previous;
    };
    class GlyphCache
    {
    private:
        GlyphCacheItem* head = nullptr;
        GlyphCacheItem* tail = nullptr;
        Dictionary<unsigned, GlyphCacheItem*> map;
        List<GlyphCacheItem> cacheItemPool;
        int cacheItemAllocIndex = 0;
    public:
        const int MaxCacheSize = 128;
        GlyphCache()
        {
            cacheItemPool.SetSize(MaxCacheSize);
        }
        GlyphCacheItem* Find(unsigned codePoint)
        {
            GlyphCacheItem* cacheItem = nullptr;
            if (map.TryGetValue(codePoint, cacheItem))
            {
                if (cacheItem == head)
                    return cacheItem;
                if (cacheItem->previous)
                    cacheItem->previous->next = cacheItem->next;
                if (cacheItem->next)
                    cacheItem->next->previous = cacheItem->previous;
                if (cacheItem == tail) tail = cacheItem->previous;
                cacheItem->next = head;
                head->previous = cacheItem;
                cacheItem->previous = nullptr;
                head = cacheItem;
                return cacheItem;
            }
            else
            {
                if (cacheItemAllocIndex >= MaxCacheSize)
                {
                    auto newTail = tail->previous;
                    newTail->next = nullptr;
                    tail->next = head;
                    tail->previous = nullptr;
                    head->previous = tail;
                    head = tail;
                    tail = newTail;
                    map.Remove(head->codePoint);
                }
                else
                {
                    auto newHead = &cacheItemPool[cacheItemAllocIndex];
                    cacheItemAllocIndex++;
                    newHead->next = head;
                    if (head)
                    {
                        head->previous = newHead;
                    }
                    head = newHead;
                    if (!tail) tail = head;
                }
                head->codePoint = codePoint;
                head->isValid = false;
                head->bitmap.Clear();
                map[codePoint] = head;
                return head;
            }
        }
    };

    class GenericFontRasterizer : public OsFontRasterizer
    {
    private:
        List<unsigned char> monochromeBuffer;
        stbtt_fontinfo fontinfo;
        List<unsigned char> fontBuffer; // buffer for the true type font file
        List<unsigned char> underlineCharBuffer; // one dimensional (y only) buffer for the underline char
        int underlineCharY0, underlineCharY1;
        float fontScale = 0.0f;
        bool drawUnderline = false;
        int fontAscent = 0, fontDescent = 0, fontLineGap = 0;
        GlyphCache glyphCache;
        bool fontInitialized = false;
    public:
        void BuildUnderlineCharBuffer()
        {
            List<unsigned char> tmpBitmap;
            int advanceWidth = 0, leftSideBearing = 0;
            stbtt_GetCodepointHMetrics(&fontinfo, '_', &advanceWidth, &leftSideBearing);
            int x0, y0, x1, y1;
            stbtt_GetCodepointBitmapBox(&fontinfo, '_', fontScale, fontScale, &x0, &y0, &x1, &y1);
            tmpBitmap.SetSize((x1 - x0) * (y1 - y0));
            memset(tmpBitmap.Buffer(), 0, tmpBitmap.Count());
            stbtt_MakeCodepointBitmap(&fontinfo, tmpBitmap.Buffer(), x1 - x0, y1 - y0, x1 - x0, fontScale, fontScale, '_');
            underlineCharY0 = y0;
            underlineCharY1 = y1;
            underlineCharBuffer.SetSize(y1 - y0);
            for (int i = 0; i < y1 - y0; i++)
                underlineCharBuffer[i] = tmpBitmap[i * (x1 - x0) + (x1 - x0) / 2];
        }
        // Set the font style of this label
        virtual void SetFont(const Font& Font, int dpi) override
        {
            CoreLib::String fontFileName = FindFontFile(Font);
            if (fontFileName.Length() == 0)
            {
                Engine::Print("Error: no fonts found!\n");
                fontBuffer.Clear();
                fontInitialized = false;
                return;
            }
            fontBuffer = CoreLib::IO::File::ReadAllBytes(fontFileName);
            stbtt_InitFont(&fontinfo, fontBuffer.Buffer(), 0);
            auto pixelHeight = (Font.Size * dpi / 72);
            fontScale = stbtt_ScaleForMappingEmToPixels(&fontinfo, (float)pixelHeight);
            stbtt_GetFontVMetrics(&fontinfo, &fontAscent, &fontDescent, &fontLineGap);
            drawUnderline = Font.Underline;
            BuildUnderlineCharBuffer();
            fontInitialized = true;
        }

        List<unsigned> StringToCodePointList(const CoreLib::String& text)
        {
            int textReadIndex = 0;
            auto charReader = [&](int)
            {
                auto rs = text[textReadIndex];
                textReadIndex++;
                return rs;
            };
            List<unsigned> codePoints;
            while (textReadIndex < text.Length())
            {
                unsigned nextCodePoint = (unsigned)(CoreLib::IO::GetUnicodePointFromUTF8(charReader));
                codePoints.Add(nextCodePoint);
            }
            return codePoints;
        }

        int DrawChar(unsigned codePoint, TextSize bufferSize, int bpX, int bpY, bool isBeginOfLine)
        {
            auto glyph = glyphCache.Find(codePoint);
            if (!glyph->isValid)
            {
                stbtt_GetCodepointHMetrics(&fontinfo, codePoint, &glyph->advanceWidth, &glyph->leftSideBearing);
                stbtt_GetCodepointBitmapBox(&fontinfo, codePoint, fontScale, fontScale, &glyph->x0, &glyph->y0, &glyph->x1, &glyph->y1);
                glyph->bitmap.SetSize((glyph->x1 - glyph->x0) * (glyph->y1 - glyph->y0));
                memset(glyph->bitmap.Buffer(), 0, glyph->bitmap.Count());
                stbtt_MakeCodepointBitmap(&fontinfo, glyph->bitmap.Buffer(), glyph->x1 - glyph->x0, glyph->y1 - glyph->y0, glyph->x1 - glyph->x0, 
                    fontScale, fontScale, codePoint);
                glyph->isValid = true;
            }
            if (isBeginOfLine && glyph->leftSideBearing < 0)
            {
                bpX -= (int)(glyph->leftSideBearing * fontScale);
            }
            int x0 = glyph->x0;
            int x1 = glyph->x1;
            int y0 = glyph->y0;
            int y1 = glyph->y1;
            // Blit tmpBitmap to monochromeBuffer
            for (int y = y0; y < y1; y++)
                for (int x = x0; x < x1; x++)
                {
                    int dstY = bpY + y;
                    int dstX = bpX + x;
                    if (dstY > 0 && dstY < bufferSize.y && dstX > 0 && dstX < bufferSize.x)
                    {
                        int dstVal = monochromeBuffer[dstY * bufferSize.x + dstX];
                        int targetVal = glyph->bitmap[(x - x0) + (y - y0) * (x1 - x0)];
                        dstVal = dstVal * (255 - targetVal) / 255 + glyph->bitmap[(x - x0) + (y - y0) * (x1 - x0)];
                        if (dstVal > 255) dstVal = 255;
                        monochromeBuffer[dstY * bufferSize.x + dstX] = (unsigned char)dstVal;
                    }
                }
            return bpX + (int)(glyph->advanceWidth * fontScale);
        }

        // Set the text that is going to be displayed.
        virtual TextRasterizationResult RasterizeText(const CoreLib::String& text, const DrawTextOptions& options) override
        {
            auto codePoints = StringToCodePointList(text);
            auto textSize = GetTextSize(codePoints, options);

            if (!fontInitialized)
            {
                TextRasterizationResult emptyRs;
                monochromeBuffer.SetSize(textSize.x * textSize.y);
                memset(monochromeBuffer.Buffer(), 0, monochromeBuffer.Count());
                emptyRs.ImageData = monochromeBuffer.Buffer();
                emptyRs.Size = textSize;
                return emptyRs;
            }
            
            monochromeBuffer.SetSize(textSize.x * textSize.y);
            memset(monochromeBuffer.Buffer(), 0, monochromeBuffer.Count());
            int baselineY = (int)(fontAscent * fontScale);
            int bpX = 0, bpY = baselineY;
            bool charUnderline = false;
            bool isBeginOfLine = true;
            for (int i = 0; i < codePoints.Count(); i++)
            {
                auto codePoint = codePoints[i];
                auto nextCodePoint = i < codePoints.Count() - 1 ? codePoints[i + 1] : 0;
                if (options.ProcessPrefix && !options.EditorText)
                {
                    if (codePoint == 0x26)
                    {
                        if (nextCodePoint != 0x26)
                        {
                            charUnderline = true;
                            codePoint = 0;
                            continue;
                        }
                        else
                        {
                            i++;
                        }
                    }
                }
                if (codePoint == 0xD || (codePoint == 0xA && nextCodePoint != 0xD))
                {
                    // Encountered CR/LF
                    bpY += (int)((fontAscent - fontDescent + fontLineGap) * fontScale);
                    bpX = 0;
                    isBeginOfLine = true;
                }
                else
                {
                    int newBpX = bpX;
                    if (codePoint)
                        newBpX = DrawChar(codePoint, textSize, bpX, bpY, isBeginOfLine);
                    if (drawUnderline || charUnderline)
                    {
                        charUnderline = false;
                        // draw underline
                        for (int y = bpY + underlineCharY0; y < bpY + underlineCharY1; y++)
                        {
                            if (y < 0) continue;
                            if (y > textSize.y) break;
                            auto underlineCharVal = underlineCharBuffer[y - (bpY + underlineCharY0)];
                            for (int x = bpX; x < newBpX; x++)
                            {
                                auto& dstValue = monochromeBuffer[y * textSize.x + x];
                                dstValue = dstValue * (255 - underlineCharVal) / 255 + underlineCharVal;
                            }
                        }
                        
                    }
                    bpX = newBpX;
                    isBeginOfLine = false;
                }
            }
            TextRasterizationResult result;
            result.ImageData = monochromeBuffer.Buffer();
            result.Size = textSize;
            return result;
        }

        virtual TextSize GetTextSize(const CoreLib::String& text, const DrawTextOptions& options) override
        {
            auto codePoints = StringToCodePointList(text);
            return GetTextSize(codePoints, options);
        }

        virtual TextSize GetTextSize(const CoreLib::List<unsigned int>& text, const DrawTextOptions& options) override
        {
            if (!fontInitialized)
            {
                return TextSize{ text.Count() * 8, 16 };
            }
            int width = 0;
            int maxWidth = 0;
            int height = (int)((fontAscent - fontDescent) * fontScale);
            bool isBeginOfLine = true;
            for (int i = 0; i < text.Count(); i++)
            {
                auto codePoint = text[i];
                auto nextCodePoint = i < text.Count() - 1 ? text[i + 1] : 0;
                if (options.ProcessPrefix && !options.EditorText)
                {
                    if (codePoint == 0x26)
                    {
                        if (nextCodePoint != 0x26)
                        {
                            continue;
                        }
                        else
                        {
                            i++;
                        }
                    }
                }
               
                if (codePoint == 0xD || (codePoint == 0xA && nextCodePoint != 0xD))
                {
                    // Encountered CR/LF
                    height += (int)((fontAscent - fontDescent + fontLineGap) * fontScale);
                    width = 0;
                    isBeginOfLine = true;
                }
                else
                {
                    int advanceWidth = 0, leftSideBearing = 0;
                    stbtt_GetCodepointHMetrics(&fontinfo, codePoint, &advanceWidth, &leftSideBearing);
                    width += (int)(advanceWidth * fontScale);
                    if (isBeginOfLine && leftSideBearing < 0)
                        width -= (int)(leftSideBearing * fontScale);
                    maxWidth = Math::Max(maxWidth, width);
                    isBeginOfLine = false;
                }
            }
            TextSize rs;
            rs.x = maxWidth;
            rs.y = height;
            return rs;
        }
    };

    OsFontRasterizer* CreateGenericFontRasterizer()
    {
        return new GenericFontRasterizer();
    }
}