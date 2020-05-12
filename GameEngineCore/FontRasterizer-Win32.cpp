#ifdef _WIN32

#include <Windows.h>
#include "OS.h"

namespace GameEngine
{
    using namespace GraphicsUI;
    using namespace CoreLib;

    UINT GetFormat(const GraphicsUI::DrawTextOptions& options)
    {
        if (options.EditorText)
            return DT_EDITCONTROL | DT_NOCLIP | DT_NOPREFIX;
        else
        {
            if (!options.ProcessPrefix)
                return DT_NOCLIP | DT_NOPREFIX;
            if (options.HidePrefix)
                return DT_NOCLIP | DT_HIDEPREFIX;
            return DT_NOCLIP;
        }
    }

    class GDICanvas
    {
    private:
        HFONT hdFont;
        HBRUSH hdBrush;
    public:
        HDC Handle;
        GDICanvas(HDC DC)
        {
            Handle = DC;
            hdBrush = CreateSolidBrush(RGB(255, 255, 255));
            SelectObject(Handle, hdBrush);
        }
        ~GDICanvas()
        {
            DeleteObject(hdFont);
            DeleteObject(hdBrush);
        }
        void ChangeFont(Font newFont, int dpi)
        {
            LOGFONT font;
            font.lfCharSet = DEFAULT_CHARSET;
            font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
            font.lfEscapement = 0;
            wcscpy_s(font.lfFaceName, 32, newFont.FontName.ToWString());
            font.lfHeight = -MulDiv(newFont.Size, dpi, 72);
            font.lfItalic = newFont.Italic;
            font.lfOrientation = 0;
            font.lfOutPrecision = OUT_DEVICE_PRECIS;
            font.lfPitchAndFamily = DEFAULT_PITCH || FF_DONTCARE;
            font.lfQuality = DEFAULT_QUALITY;
            font.lfStrikeOut = newFont.StrikeOut;
            font.lfUnderline = newFont.Underline;
            font.lfWeight = (newFont.Bold ? FW_BOLD : FW_NORMAL);
            font.lfWidth = 0;
            hdFont = CreateFontIndirect(&font);
            HGDIOBJ OldFont = SelectObject(Handle, hdFont);
            DeleteObject(OldFont);
        }
        void DrawText(const CoreLib::String& text, int X, int Y, DrawTextOptions options)
        {
            int len = 0;
            auto wstr = text.ToWString(&len);
            RECT rect;
            rect.left = X;
            rect.top = Y;
            rect.bottom = 0;
            rect.right = 0;
            ::DrawText(Handle, wstr, len, &rect, GetFormat(options));
        }

        TextSize GetTextSize(const CoreLib::String& Text, DrawTextOptions options)
        {
            int len;
            auto wstr = Text.ToWString(&len);
            RECT rect;
            rect.left = 0;
            rect.top = 0;
            rect.bottom = 0;
            rect.right = 0;
            ::DrawText(Handle, wstr, len, &rect, GetFormat(options) | DT_CALCRECT);
            TextSize rs;
            rs.x = rect.right;
            rs.y = rect.bottom;
            return rs;
        }
        TextSize GetTextSize(const CoreLib::List<unsigned int>& Text, DrawTextOptions options)
        {
            CoreLib::List<unsigned short> wstr;
            wstr.Reserve(Text.Count());
            for (int i = 0; i < Text.Count(); i++)
            {
                unsigned short buffer[2];
                int len = CoreLib::IO::EncodeUnicodePointToUTF16(buffer, Text[i]);
                wstr.AddRange(buffer, len);
            }
            RECT rect;
            rect.left = 0;
            rect.top = 0;
            rect.bottom = 0;
            rect.right = 0;
            ::DrawText(Handle, (LPCWSTR)wstr.Buffer(), wstr.Count(), &rect, GetFormat(options) | DT_CALCRECT);
            TextSize rs;
            rs.x = rect.right;
            rs.y = rect.bottom;
            return rs;
        }
        void Clear(int w, int h)
        {
            RECT Rect;
            Rect.bottom = h;
            Rect.right = w;
            Rect.left = 0;  Rect.top = 0;
            FillRect(Handle, &Rect, hdBrush);
        }
    };

    class DIBImage
    {
    private:
        void CreateBMP(int Width, int Height)
        {
            BITMAPINFO bitInfo;
            void* imgptr = NULL;
            bitHandle = NULL;
            bitInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitInfo.bmiHeader.biWidth = Width;
            bitInfo.bmiHeader.biHeight = -Height;
            bitInfo.bmiHeader.biPlanes = 1;
            bitInfo.bmiHeader.biBitCount = 24;
            bitInfo.bmiHeader.biCompression = BI_RGB;
            bitInfo.bmiHeader.biSizeImage = 0;
            bitInfo.bmiHeader.biXPelsPerMeter = 0;
            bitInfo.bmiHeader.biYPelsPerMeter = 0;
            bitInfo.bmiHeader.biClrUsed = 0;
            bitInfo.bmiHeader.biClrImportant = 0;
            bitInfo.bmiColors[0].rgbBlue = 255;
            bitInfo.bmiColors[0].rgbGreen = 255;
            bitInfo.bmiColors[0].rgbRed = 255;
            bitInfo.bmiColors[0].rgbReserved = 255;
            bitHandle = CreateDIBSection(0, &bitInfo, DIB_RGB_COLORS, &imgptr, NULL, 0);
            HGDIOBJ OldBmp = SelectObject(Handle, bitHandle);
            DeleteObject(OldBmp);
            if (ScanLine) delete[] ScanLine;
            if (Height)
                ScanLine = new unsigned char* [Height];
            else
                ScanLine = 0;
            int rowWidth = Width * bitInfo.bmiHeader.biBitCount / 8; //Width*3
            while (rowWidth % 4) rowWidth++;
            for (int i = 0; i < Height; i++)
            {
                ScanLine[i] = (unsigned char*)(imgptr)+rowWidth * i;
            }
            canvas->Clear(Width, Height);
        }
    public:
        HDC Handle;
        HBITMAP bitHandle;
        GDICanvas* canvas;
        unsigned char** ScanLine;
        DIBImage()
        {
            ScanLine = NULL;
            Handle = CreateCompatibleDC(NULL);
            canvas = new GDICanvas(Handle);
            canvas->ChangeFont(Font("Segoe UI", 10), 96);

        }
        ~DIBImage()
        {
            delete canvas;
            if (ScanLine)
                delete[] ScanLine;
            DeleteDC(Handle);
            DeleteObject(bitHandle);
        }

        void SetSize(int Width, int Height)
        {
            CreateBMP(Width, Height);
        }
    };
 
    class Win32FontRasterizer : public OsFontRasterizer
    {
    private:
        unsigned int TexID;
        DIBImage* Bit;
        List<unsigned char> monochromeBuffer;
    public:
        Win32FontRasterizer()
        {
            Bit = new DIBImage();
        }

        ~Win32FontRasterizer()
        {
            delete Bit;
        }

        // Set the font style of this label
        virtual void SetFont(const Font& Font, int dpi) override
        {
            Bit->canvas->ChangeFont(Font, dpi);
        }

        // Set the text that is going to be displayed.
        virtual TextRasterizationResult RasterizeText(const CoreLib::String& text, const DrawTextOptions& options) override
        {
            int TextWidth, TextHeight;
            List<unsigned char> pic;
            TextSize size;
            size = Bit->canvas->GetTextSize(text, options);
            TextWidth = size.x;
            TextHeight = size.y;
            Bit->SetSize(TextWidth, TextHeight);
            monochromeBuffer.SetSize(TextWidth * TextHeight);
            Bit->canvas->Clear(TextWidth, TextHeight);
            Bit->canvas->DrawText(text, 0, 0, options);
            for (int i = 0; i < TextHeight; i++)
            {
                for (int j = 0; j < TextWidth; j++)
                {
                    int idx = i * TextWidth + j;
                    auto val = 255 - (Bit->ScanLine[i][j * 3 + 2] + Bit->ScanLine[i][j * 3 + 1] + Bit->ScanLine[i][j * 3]) / 3;
                    monochromeBuffer[i * TextWidth + j] = (unsigned char)val;
                }
            }
            TextRasterizationResult result;
            result.ImageData = monochromeBuffer.Buffer();
            result.Size.x = TextWidth;
            result.Size.y = TextHeight;
            return result;
        }

        virtual TextSize GetTextSize(const CoreLib::String& text, const DrawTextOptions& options) override
        {
            return Bit->canvas->GetTextSize(text, options);
        }

        virtual TextSize GetTextSize(const CoreLib::List<unsigned int>& text, const DrawTextOptions& options) override
        {
            return Bit->canvas->GetTextSize(text, options);
        }
    };

    OsFontRasterizer* CreateWin32FontRasterizer()
    {
        return new Win32FontRasterizer();
    }
}

#endif