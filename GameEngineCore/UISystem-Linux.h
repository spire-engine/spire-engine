#ifndef UISYSTEM_LINUX_H
#define UISYSTEM_LINUX_H

#if defined (__linux__)
#include "UISystemBase.h"

namespace GameEngine
{
	class DIBImage;
	class SystemWindow;

	class UIWindowsSystemInterface;

	class LinuxUISystem : public UISystemBase
	{
	public:
		virtual void SetClipboardText(const CoreLib::String& text) override;
		virtual CoreLib::String GetClipboardText() override;
		virtual GraphicsUI::IFont* LoadDefaultFont(GraphicsUI::UIWindowContext* ctx, GraphicsUI::DefaultFontType dt = GraphicsUI::DefaultFontType::Content) override;
		virtual void SwitchCursor(GraphicsUI::CursorType c) override;
	public:
		LinuxUISystem(HardwareRenderer* ctx);
	};
}

#endif

#endif