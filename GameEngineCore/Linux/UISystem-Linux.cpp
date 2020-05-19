#if defined(__linux__)

#include "UISystem-Linux.h"
#include "OsApplicationContext-Linux.h"

namespace GameEngine
{
    // Defined in OS-Linux.cpp
    LinuxApplicationContext* GetLinuxApplicationContext();

    void LinuxUISystem::SetClipboardText(const CoreLib::String& text)
    {
        auto context = GetLinuxApplicationContext();
        auto display = context->xdisplay;
        GetLinuxApplicationContext()->clipboardString = text;
        XSetSelectionOwner(display,
                           XInternAtom(display, "CLIPBOARD", False),
                           context->clipboardWindow,
                           CurrentTime);
    }

    CoreLib::String LinuxUISystem::GetClipboardText()
    {
        auto context = GetLinuxApplicationContext();
        auto display = GetLinuxApplicationContext()->xdisplay;
        Atom clipboardAtom = XInternAtom(display, "CLIPBOARD", False);
        Atom formatAtom = XInternAtom(display, "UTF8_STRING", False);
        Atom propAtom = XInternAtom(display, "XSEL_DATA", False);
        XConvertSelection(display, clipboardAtom, formatAtom, propAtom, context->clipboardWindow, CurrentTime);
        context->clipboardStringReady = false;
        do
        {
            OsApplication::DoEvents();
        } while (!context->clipboardStringReady);
        return context->clipboardString;
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
        auto context = GetLinuxApplicationContext();
        if (context->xdisplay && context->currentMouseEventWindow)
        {
            XDefineCursor(context->xdisplay, context->currentMouseEventWindow->GetNativeHandle().window, context->cursors[(int)c]);
        }
    }
    LinuxUISystem::LinuxUISystem(HardwareRenderer* ctx)
        : UISystemBase(ctx)
    {
    }
} // namespace GameEngine

#endif