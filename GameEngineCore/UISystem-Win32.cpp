#ifdef _WIN32
#include "UISystem-Win32.h"
#include <Windows.h>
#include <wingdi.h>
#include "CoreLib/VectorMath.h"
#include "HardwareRenderer.h"
#include "OS.h"

#pragma comment(lib,"imm32.lib")
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lParam)	((int)(short)LOWORD(lParam))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lParam)	((int)(short)HIWORD(lParam))
#endif

using namespace CoreLib;
using namespace VectorMath;
using namespace GraphicsUI;

namespace GameEngine
{
    struct TextSize
    {
        int x, y;
    };

    class TextRasterizationResult
    {
    public:
        TextSize Size;
        int BufferSize;
        unsigned char * ImageData;
    };

    class TextRasterizer
    {
    private:
        unsigned int TexID;
        DIBImage *Bit;
    public:
        TextRasterizer();
        ~TextRasterizer();
        bool MultiLine = false;
        void SetFont(const Font & Font, int dpi);
        TextRasterizationResult RasterizeText(UISystemBase * system, const CoreLib::String & text, unsigned char * existingBuffer, int existingBufferSize, const GraphicsUI::DrawTextOptions & options);
        TextSize GetTextSize(const CoreLib::String & text, const GraphicsUI::DrawTextOptions & options);
        TextSize GetTextSize(const CoreLib::List<unsigned int> & text, const GraphicsUI::DrawTextOptions & options);
    };

    UINT GetFormat(const DrawTextOptions & options)
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
        void DrawText(const CoreLib::String & text, int X, int Y, DrawTextOptions options)
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
        TextSize GetTextSize(const CoreLib::List<unsigned int> & Text, DrawTextOptions options)
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
            void * imgptr = NULL;
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
                ScanLine = new unsigned char *[Height];
            else
                ScanLine = 0;
            int rowWidth = Width * bitInfo.bmiHeader.biBitCount / 8; //Width*3
            while (rowWidth % 4) rowWidth++;
            for (int i = 0; i < Height; i++)
            {
                ScanLine[i] = (unsigned char *)(imgptr)+rowWidth * i;
            }
            canvas->Clear(Width, Height);
        }
    public:
        HDC Handle;
        HBITMAP bitHandle;
        GDICanvas * canvas;
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

    class WindowsFont : public GraphicsUI::IFont
    {
    public:
        CoreLib::RefPtr<TextRasterizer> rasterizer;
        UISystemBase * system;
        HWND wndHandle;
        Font fontDesc;
    public:
        WindowsFont(UISystemBase * ctx, HWND wnd, int dpi, const Font & font)
        {
            system = ctx;
            wndHandle = wnd;
            fontDesc = font;
            rasterizer = new TextRasterizer();
            UpdateFontContext(dpi);
        }
        void UpdateFontContext(int dpi)
        {
            rasterizer->SetFont(fontDesc, dpi);
        }
        HWND GetWindowHandle()
        {
            return wndHandle;
        }
        virtual GraphicsUI::Rect MeasureString(const CoreLib::String & text, GraphicsUI::DrawTextOptions options) override;
        virtual GraphicsUI::Rect MeasureString(const CoreLib::List<unsigned int> & text, GraphicsUI::DrawTextOptions options) override;
        virtual GraphicsUI::IBakedText * BakeString(const CoreLib::String & text, GraphicsUI::IBakedText * previous, GraphicsUI::DrawTextOptions options) override;

    };

	SHIFTSTATE GetCurrentShiftState()
	{
		SHIFTSTATE Shift = 0;
		if (GetAsyncKeyState(VK_SHIFT))
			Shift = Shift | SS_SHIFT;
		if (GetAsyncKeyState(VK_CONTROL))
			Shift = Shift | SS_CONTROL;
		if (GetAsyncKeyState(VK_MENU))
			Shift = Shift | SS_ALT;
		return Shift;
	}

	void TranslateMouseMessage(UIMouseEventArgs &Data, WPARAM wParam, LPARAM lParam)
	{
		Data.Shift = GetCurrentShiftState();
		bool L, M, R, S, C;
		L = (wParam&MK_LBUTTON) != 0;
		M = (wParam&MK_MBUTTON) != 0;
		R = (wParam&MK_RBUTTON) != 0;
		S = (wParam & MK_SHIFT) != 0;
		C = (wParam & MK_CONTROL) != 0;
		Data.Delta = GET_WHEEL_DELTA_WPARAM(wParam);
		if (L)
		{
			Data.Shift = Data.Shift | SS_BUTTONLEFT;
		}
		else {
			if (M) {
				Data.Shift = Data.Shift | SS_BUTTONMIDDLE;
			}
			else {
				if (R) {
					Data.Shift = Data.Shift | SS_BUTTONRIGHT;
				}
			}
		}
		if (S)
			Data.Shift = Data.Shift | SS_SHIFT;
		if (C)
			Data.Shift = Data.Shift | SS_CONTROL;
		Data.X = GET_X_LPARAM(lParam);
		Data.Y = GET_Y_LPARAM(lParam);
	}

    HRESULT(WINAPI *getDpiForMonitor)(void* hmonitor, int dpiType, unsigned int *dpiX, unsigned int *dpiY);
    
	int Win32UISystem::GetCurrentDpi(HWND windowHandle)
	{
		int dpi = 96;
        if (isWindows81OrGreater)
        {
            getDpiForMonitor(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTOPRIMARY), 0, (UINT*)&dpi, (UINT*)&dpi);
            return dpi;
        }
    	dpi = GetDeviceCaps(NULL, LOGPIXELSY);
		return dpi;
	}
    
	int Win32UISystem::HandleSystemMessage(SystemWindow* window, UINT message, WPARAM &wParam, LPARAM &lParam)
	{
		int rs = -1;
		unsigned short Key;
		UIMouseEventArgs Data;
        UIWindowContext * ctx = nullptr;
        if (!windowContexts.TryGetValue(window, ctx))
        {
            return -1;
        }
        auto entry = ctx->uiEntry.Ptr();
		switch (message)
		{
		case WM_CHAR:
		{
			Key = (unsigned short)(DWORD)wParam;
			entry->DoKeyPress(Key, GetCurrentShiftState());
			break;
		}
		case WM_KEYUP:
		{
			Key = (unsigned short)(DWORD)wParam;
			entry->DoKeyUp(Key, GetCurrentShiftState());
			break;
		}
		case WM_KEYDOWN:
		{
			Key = (unsigned short)(DWORD)wParam;
			entry->DoKeyDown(Key, GetCurrentShiftState());
			break;
		}
		case WM_SYSKEYDOWN:
		{
			Key = (unsigned short)(DWORD)wParam;
			if ((lParam&(1 << 29)))
			{
				entry->DoKeyDown(Key, SS_ALT);
			}
			else
				entry->DoKeyDown(Key, 0);
			if (Key != VK_F4)
				rs = 0;
			break;
		}
		case WM_SYSCHAR:
		{
			rs = 0;
			break;
		}
		case WM_SYSKEYUP:
		{
			Key = (unsigned short)(DWORD)wParam;
			if ((lParam & (1 << 29)))
			{
				entry->DoKeyUp(Key, SS_ALT);
			}
			else
				entry->DoKeyUp(Key, 0);
			rs = 0;
			break;
		}
		case WM_MOUSEMOVE:
		{
			ctx->tmrHover->Start();
			TranslateMouseMessage(Data, wParam, lParam);
			entry->DoMouseMove(Data.X, Data.Y);
			break;
		}
		case WM_LBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_RBUTTONDOWN:
		{
            ctx->tmrHover->Start();
			TranslateMouseMessage(Data, wParam, lParam);
			entry->DoMouseDown(Data.X, Data.Y, Data.Shift);
			SetCapture((HWND)window->GetNativeHandle());
			break;
		}
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
		case WM_LBUTTONUP:
		{
            ctx->tmrHover->Stop();
			ReleaseCapture();
			TranslateMouseMessage(Data, wParam, lParam);
			if (message == WM_RBUTTONUP)
				Data.Shift = Data.Shift | SS_BUTTONRIGHT;
			else if (message == WM_LBUTTONUP)
				Data.Shift = Data.Shift | SS_BUTTONLEFT;
			else if (message == WM_MBUTTONUP)
				Data.Shift = Data.Shift | SS_BUTTONMIDDLE;
			entry->DoMouseUp(Data.X, Data.Y, Data.Shift);
			break;
		}
		case WM_LBUTTONDBLCLK:
		case WM_MBUTTONDBLCLK:
		case WM_RBUTTONDBLCLK:
		{
			entry->DoDblClick();
		}
		break;
		case WM_MOUSEWHEEL:
		{
			UIMouseEventArgs e;
			TranslateMouseMessage(e, wParam, lParam);
			entry->DoMouseWheel(e.Delta, e.Shift);
		}
		break;
		case WM_SIZE:
		{
			RECT rect;
			GetClientRect((HWND)window->GetNativeHandle(), &rect);
			entry->SetWidth(rect.right - rect.left);
			entry->SetHeight(rect.bottom - rect.top);
		}
		break;
		case WM_PAINT:
		{
			//Draw(0,0);
		}
		break;
		case WM_ERASEBKGND:
		{
		}
		break;
		case WM_NCMBUTTONDOWN:
		case WM_NCRBUTTONDOWN:
		case WM_NCLBUTTONDOWN:
		{
            ctx->tmrHover->Stop();
			entry->DoClosePopup();
		}
		break;
		break;
		case WM_IME_SETCONTEXT:
			lParam = 0;
			break;
		case WM_INPUTLANGCHANGE:
			break;
		case WM_IME_COMPOSITION:
		{
			HIMC hIMC = ImmGetContext((HWND)window->GetNativeHandle());
			VectorMath::Vec2i pos = entry->GetCaretScreenPos();
			UpdateCompositionWindowPos(hIMC, pos.x, pos.y + 6);
			if (lParam&GCS_COMPSTR)
			{
				wchar_t EditString[201];
				unsigned int StrSize = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, EditString, sizeof(EditString) - sizeof(char));
				EditString[StrSize / sizeof(wchar_t)] = 0;
				entry->ImeMessageHandler.DoImeCompositeString(String::FromWString(EditString));
			}
			if (lParam&GCS_RESULTSTR)
			{
				wchar_t ResultStr[201];
				unsigned int StrSize = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, ResultStr, sizeof(ResultStr) - sizeof(TCHAR));
				ResultStr[StrSize / sizeof(wchar_t)] = 0;
				entry->ImeMessageHandler.StringInputed(String::FromWString(ResultStr));
			}
			ImmReleaseContext((HWND)window->GetNativeHandle(), hIMC);
			rs = 0;
		}
		break;
		case WM_IME_STARTCOMPOSITION:
			entry->ImeMessageHandler.DoImeStart();
			break;
		case WM_IME_ENDCOMPOSITION:
			entry->ImeMessageHandler.DoImeEnd();
			break;
		case WM_DPICHANGED:
		{
			int dpi = 96;
            if (getDpiForMonitor)
			    getDpiForMonitor(MonitorFromWindow((HWND)window->GetNativeHandle(), MONITOR_DEFAULTTOPRIMARY), 0, (UINT*)&dpi, (UINT*)&dpi);
            for (auto & f : fonts)
            {
                if (((WindowsFont*)f.Value.Ptr())->GetWindowHandle() == window->GetNativeHandle())
				    ((WindowsFont*)f.Value.Ptr())->UpdateFontContext(dpi);
            }
			RECT* const prcNewWindow = (RECT*)lParam;
			SetWindowPos((HWND)window->GetNativeHandle(),
				NULL,
				prcNewWindow->left,
				prcNewWindow->top,
				prcNewWindow->right - prcNewWindow->left,
				prcNewWindow->bottom - prcNewWindow->top,
				SWP_NOZORDER | SWP_NOACTIVATE);
			entry->DoDpiChanged();
			rs = 0;
			break;
		}
		}
		return rs;
	}

	void Win32UISystem::SetClipboardText(const String & text)
	{
		if (OpenClipboard(NULL))
		{
			EmptyClipboard();
			HGLOBAL hBlock = GlobalAlloc(GMEM_MOVEABLE, sizeof(WCHAR) * (text.Length() + 1));
			if (hBlock)
			{
				WCHAR *pwszText = (WCHAR*)GlobalLock(hBlock);
				if (pwszText)
				{
					CopyMemory(pwszText, text.ToWString(), text.Length() * sizeof(WCHAR));
					pwszText[text.Length()] = L'\0';  // Terminate it
					GlobalUnlock(hBlock);
				}
				SetClipboardData(CF_UNICODETEXT, hBlock);
			}
			CloseClipboard();
			if (hBlock)
				GlobalFree(hBlock);
		}
	}

	String Win32UISystem::GetClipboardText()
	{
		String txt;
		if (OpenClipboard(NULL))
		{
			HANDLE handle = GetClipboardData(CF_UNICODETEXT);
			if (handle)
			{
				// Convert the ANSI string to Unicode, then
				// insert to our buffer.
				WCHAR *pwszText = (WCHAR*)GlobalLock(handle);
				if (pwszText)
				{
					// Copy all characters up to null.
					txt = String::FromWString(pwszText);
					GlobalUnlock(handle);
				}
			}
			CloseClipboard();
		}
		return txt;
	}

	IFont * Win32UISystem::LoadDefaultFont(GraphicsUI::UIWindowContext * ctx, DefaultFontType dt)
	{
		switch (dt)
		{
		case DefaultFontType::Content:
			return LoadFont((UIWindowContext*)ctx, Font("Segoe UI", 11));
		case DefaultFontType::Title:
			return LoadFont((UIWindowContext*)ctx, Font("Segoe UI", 11, true, false, false));
		default:
			return LoadFont((UIWindowContext*)ctx, Font("Webdings", 11));
		}
	}

	void Win32UISystem::SwitchCursor(CursorType c)
	{
		LPTSTR cursorName;
		switch (c)
		{
		case CursorType::Arrow:
			cursorName = IDC_ARROW;
			break;
		case CursorType::IBeam:
			cursorName = IDC_IBEAM;
			break;
		case CursorType::Cross:
			cursorName = IDC_CROSS;
			break;
		case CursorType::Wait:
			cursorName = IDC_WAIT;
			break;
		case CursorType::SizeAll:
			cursorName = IDC_SIZEALL;
			break;
		case CursorType::SizeNS:
			cursorName = IDC_SIZENS;
			break;
		case CursorType::SizeWE:
			cursorName = IDC_SIZEWE;
			break;
		case CursorType::SizeNWSE:
			cursorName = IDC_SIZENWSE;
			break;
		case CursorType::SizeNESW:
			cursorName = IDC_SIZENESW;
			break;
		default:
			cursorName = IDC_ARROW;
		}
		SetCursor(LoadCursor(0, cursorName));
	}

	void Win32UISystem::UpdateCompositionWindowPos(HIMC imc, int x, int y)
	{
		COMPOSITIONFORM cf;
		cf.dwStyle = CFS_POINT;
		cf.ptCurrentPos.x = x;
		cf.ptCurrentPos.y = y;
		ImmSetCompositionWindow(imc, &cf);
	}

    Win32UISystem::Win32UISystem(GameEngine::HardwareRenderer * ctx)
        : UISystemBase(ctx)
	{
        void*(WINAPI *RtlGetVersion)(LPOSVERSIONINFOEXW);
        OSVERSIONINFOEXW osInfo;
        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

        if (RtlGetVersion)
        {
            osInfo.dwOSVersionInfoSize = sizeof(osInfo);
            RtlGetVersion(&osInfo);
            if (osInfo.dwMajorVersion > 8 || (osInfo.dwMajorVersion == 8 && osInfo.dwMinorVersion >= 1))
                isWindows81OrGreater = true;
        }
        HRESULT(WINAPI*setProcessDpiAwareness)(int value);
        *(FARPROC*)&setProcessDpiAwareness = GetProcAddress(GetModuleHandleA("Shcore"), "SetProcessDpiAwareness");
        *(FARPROC*)&getDpiForMonitor = GetProcAddress(GetModuleHandleA("Shcore"), "GetDpiForMonitor");
        if (setProcessDpiAwareness)
        {
            if (isWindows81OrGreater)
                setProcessDpiAwareness(2); // PROCESS_PER_MONITOR_DPI_AWARE
            else
                setProcessDpiAwareness(1); // PROCESS_SYSTEM_DPI_AWARE
        }
	}


	IFont * Win32UISystem::LoadFont(UIWindowContext * ctx, const Font & f)
	{
		auto identifier = f.ToString() + "_" + String((long long)(void*)ctx->window->GetNativeHandle());
		RefPtr<IFont> font;
		if (!fonts.TryGetValue(identifier, font))
		{
			font = new WindowsFont(this, (HWND)ctx->window->GetNativeHandle(), GetCurrentDpi((HWND)ctx->window->GetNativeHandle()), f);
			fonts[identifier] = font;
		}
		return font.Ptr();
	}

	Rect WindowsFont::MeasureString(const CoreLib::String & text, DrawTextOptions options)
	{
		Rect rs;
		auto size = rasterizer->GetTextSize(text, options);
		rs.x = rs.y = 0;
		rs.w = size.x;
		rs.h = size.y;
		return rs;
	}

	Rect WindowsFont::MeasureString(const List<unsigned int> & text, DrawTextOptions options)
	{
		Rect rs;
		auto size = rasterizer->GetTextSize(text, options);
		rs.x = rs.y = 0;
		rs.w = size.x;
		rs.h = size.y;
		return rs;
	}

	IBakedText * WindowsFont::BakeString(const CoreLib::String & text, IBakedText * previous, DrawTextOptions options)
	{
		BakedText * prev = (BakedText*)previous;
		auto prevBuffer = (prev ? prev->textBuffer : nullptr);
		system->WaitForDrawFence();
		auto imageData = rasterizer->RasterizeText(system, text, prevBuffer, (prev?prev->BufferSize:0), options);
		BakedText * result = prev;
		if (!prevBuffer || imageData.ImageData != prevBuffer)
			result = new BakedText();
        result->font = this;
        result->options = options;
        result->textContent = text;
		result->system = system;
		result->Width = imageData.Size.x;
		result->Height = imageData.Size.y;
		result->textBuffer = imageData.ImageData;
		result->BufferSize = imageData.BufferSize;
        system->bakedTexts.Add(result);
		return result;
	}

	TextRasterizer::TextRasterizer()
	{
		Bit = new DIBImage();
	}

	TextRasterizer::~TextRasterizer()
	{
		delete Bit;
	}

	void TextRasterizer::SetFont(const Font & Font, int dpi) // Set the font style of this label
	{
		Bit->canvas->ChangeFont(Font, dpi);
	}

	TextRasterizationResult TextRasterizer::RasterizeText(UISystemBase * system, const CoreLib::String & text, unsigned char * existingBuffer, int existingBufferSize, const DrawTextOptions & options) // Set the text that is going to be displayed.
	{
		int TextWidth, TextHeight;
		List<unsigned char> pic;
		TextSize size;
		size = Bit->canvas->GetTextSize(text, options);
		TextWidth = size.x;
		TextHeight = size.y;
		Bit->SetSize(TextWidth, TextHeight);
		Bit->canvas->Clear(TextWidth, TextHeight);
		Bit->canvas->DrawText(text, 0, 0, options);


		int pixelCount = (TextWidth * TextHeight);
		int bytes = pixelCount >> Log2TextPixelsPerByte;
		int textPixelMask = (1 << TextPixelBits) - 1;
		if (pixelCount & ((1 << Log2TextPixelsPerByte) - 1))
			bytes++;
		bytes = Math::RoundUpToAlignment(bytes, 1 << Log2TextBufferBlockSize);
		auto buffer = bytes > existingBufferSize ? system->AllocTextBuffer(bytes) : existingBuffer;
		if (buffer)
		{
			const float valScale = ((1 << TextPixelBits) - 1) / 255.0f;
			for (int i = 0; i < TextHeight; i++)
			{
				for (int j = 0; j < TextWidth; j++)
				{
					int idx = i * TextWidth + j;
					auto val = 255 - (Bit->ScanLine[i][j * 3 + 2] + Bit->ScanLine[i][j * 3 + 1] + Bit->ScanLine[i][j * 3]) / 3;
					auto packedVal = Math::FastFloor(val * valScale + 0.5f);
					int addr = idx >> Log2TextPixelsPerByte;
					int mod = idx & ((1 << Log2TextPixelsPerByte) - 1);
					int mask = textPixelMask << (mod * TextPixelBits);
					buffer[addr] = (unsigned char)((buffer[addr] & (~mask)) | (packedVal << (mod * TextPixelBits)));
				}
			}
		}
		TextRasterizationResult result;
		result.ImageData = buffer;
		result.Size.x = TextWidth;
		result.Size.y = TextHeight;
        if (buffer)
            result.BufferSize = bytes > existingBufferSize ? bytes : existingBufferSize;
        else
            result.BufferSize = 0;
		return result;
	}

	TextSize TextRasterizer::GetTextSize(const CoreLib::String & text, const DrawTextOptions & options)
	{
		return Bit->canvas->GetTextSize(text, options);
	}

	TextSize TextRasterizer::GetTextSize(const CoreLib::List<unsigned int> & text, const DrawTextOptions & options)
	{
		return Bit->canvas->GetTextSize(text, options);
	}

    void BakedText::Rebake()
    {
        auto imageData = font->rasterizer->RasterizeText(system, textContent, textBuffer, this->BufferSize, this->options);
        this->BufferSize = imageData.BufferSize;
        this->Width = imageData.Size.x;
        this->Height = imageData.Size.y;
        this->textBuffer = imageData.ImageData;
    }

}

#ifdef RCVR_UNICODE
#define UNICODE
#endif

#endif