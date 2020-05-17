#ifndef UISYSTEM_WINDOWS_H
#define UISYSTEM_WINDOWS_H

#ifdef _WIN32

#include "../UISystemBase.h"
#include <Windows.h>

namespace GameEngine
{
	class DIBImage;
    class SystemWindow;

	class UIWindowsSystemInterface;

	class Win32UISystem : public UISystemBase
	{
	private:
        static bool isWindows81OrGreater;
	public:
		static int GetCurrentDpi(HWND windowHandle);
		virtual void SetClipboardText(const CoreLib::String & text) override;
		virtual CoreLib::String GetClipboardText() override;
		virtual GraphicsUI::IFont * LoadDefaultFont(GraphicsUI::UIWindowContext * ctx, GraphicsUI::DefaultFontType dt = GraphicsUI::DefaultFontType::Content) override;
		virtual void SwitchCursor(GraphicsUI::CursorType c) override;
		void UpdateCompositionWindowPos(HIMC hIMC, int x, int y);
	public:
        Win32UISystem(HardwareRenderer * ctx);
		int HandleSystemMessage(SystemWindow* window, UINT message, WPARAM &wParam, LPARAM &lParam);
	};
}

#endif
#endif