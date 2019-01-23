#ifdef _WIN32
#ifndef UISYSTEM_WINDOWS_H
#define UISYSTEM_WINDOWS_H

#include "UISystemBase.h"
#include <Windows.h>

namespace GameEngine
{
	class DIBImage;
    class SystemWindow;

	class UIWindowsSystemInterface;

	class Win32UISystem : public UISystemBase
	{
	private:
        bool isWindows81OrGreater = false;
    private:
		int GetCurrentDpi(HWND windowHandle);
	public:
		virtual void SetClipboardText(const CoreLib::String & text) override;
		virtual CoreLib::String GetClipboardText() override;
		virtual GraphicsUI::IFont * LoadDefaultFont(GraphicsUI::UIWindowContext * ctx, GraphicsUI::DefaultFontType dt = GraphicsUI::DefaultFontType::Content) override;
		virtual void SwitchCursor(GraphicsUI::CursorType c) override;
		void UpdateCompositionWindowPos(HIMC hIMC, int x, int y);
	public:
        Win32UISystem(HardwareRenderer * ctx);
        virtual GraphicsUI::IFont * LoadFont(UIWindowContext * ctx, const Font & f) override;
		int HandleSystemMessage(SystemWindow* window, UINT message, WPARAM &wParam, LPARAM &lParam);
	};
}

#endif
#endif