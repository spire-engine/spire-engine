#if defined(__linux__)

#include "UISystem-Linux.h"

namespace GameEngine
{
    void LinuxUISystem::SetClipboardText(const CoreLib::String& text)
    {
    }
    CoreLib::String LinuxUISystem::GetClipboardText()
    {
        return CoreLib::String();
    }
    GraphicsUI::IFont* LinuxUISystem::LoadDefaultFont(GraphicsUI::UIWindowContext* ctx, GraphicsUI::DefaultFontType dt)
    {
        switch (dt)
        {
        case GraphicsUI::DefaultFontType::Content:
            return LoadFont((UIWindowContext*)ctx, Font("OpenSans/OpenSans-Regular.ttf", 11));
        case GraphicsUI::DefaultFontType::Title:
            return LoadFont((UIWindowContext*)ctx, Font("OpenSans/OpenSans-Bold.ttf", 11, true, false, false));
        case GraphicsUI::DefaultFontType::Symbol:
            return LoadFont((UIWindowContext*)ctx, Font("UISymbols/uisymbols.ttf", 11));
        default:
            return LoadFont((UIWindowContext*)ctx, Font("OpenSans/OpenSans-Regular.ttf", 11));
        }
    }
    void LinuxUISystem::SwitchCursor(GraphicsUI::CursorType c)
    {
    }
    LinuxUISystem::LinuxUISystem(HardwareRenderer* ctx)
        : UISystemBase(ctx)
    {
    }
}

#endif