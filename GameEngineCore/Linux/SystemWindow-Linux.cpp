#if defined(__linux__)

#include "SystemWindow-Linux.h"
#include "OsApplicationContext-Linux.h"
#include "MessageBoxWindow-Linux.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>

namespace GameEngine
{
    // Defined in OS-Linux.cpp
    LinuxApplicationContext* GetLinuxApplicationContext();

    LinuxSystemWindow::LinuxSystemWindow(UISystemBase* pSysInterface, int log2UIBufferSize, int forceDPI)
    {
        auto context = GetLinuxApplicationContext();
        int blackColor = BlackPixel(context->xdisplay, DefaultScreen(context->xdisplay));
        int whiteColor = WhitePixel(context->xdisplay, DefaultScreen(context->xdisplay));
        currentWidth = 1920;
        currentHeight = 1080;
        forceDPIValue = forceDPI;
        handle = XCreateSimpleWindow(context->xdisplay, DefaultRootWindow(context->xdisplay), 0, 0,
                                     currentWidth, currentHeight, 0, blackColor, blackColor);
        context->systemWindows[handle] = this;
        Atom wmDelete = XInternAtom(context->xdisplay, "WM_DELETE_WINDOW", True);
        XSetWMProtocols(context->xdisplay, handle, &wmDelete, 1);
        XSelectInput(context->xdisplay, handle, StructureNotifyMask | KeyPressMask | KeyReleaseMask | PointerMotionMask |
            ButtonPressMask | ButtonReleaseMask | ExposureMask | FocusChangeMask);
        this->uiContext = pSysInterface->CreateWindowContext(this, GetClientWidth(), GetClientHeight(), log2UIBufferSize);
    }

    LinuxSystemWindow::~LinuxSystemWindow()
    {
        Close();
    }
    GraphicsUI::UIEntry* LinuxSystemWindow::GetUIEntry()
    {
        return uiContext->uiEntry.Ptr();
    }
    void LinuxSystemWindow::SetClientWidth(int w)
    {
        XResizeWindow(GetLinuxApplicationContext()->xdisplay, handle, w, currentHeight);
        HandleResizeEvent(w, currentHeight);
    }
    void LinuxSystemWindow::SetClientHeight(int h)
    {
        XResizeWindow(GetLinuxApplicationContext()->xdisplay, handle, currentWidth, h);
        HandleResizeEvent(currentWidth, h);
    }
    int LinuxSystemWindow::GetClientWidth()
    {
        if (!handle) return 0;
        auto context = GetLinuxApplicationContext();
        Window winRoot = 0, winParent = 0;
        Window* winChildren = nullptr;
        unsigned int numChilren = 0;
        XQueryTree(context->xdisplay, handle, &winRoot, &winParent, &winChildren, &numChilren);
        int x, y;
        unsigned w, h, borderWidth, depth;
        XGetGeometry(context->xdisplay, handle, &winRoot, &x, &y, &w, &h, &borderWidth, &depth);
        return w;
    }
    int LinuxSystemWindow::GetClientHeight()
    {
        if (!handle) return 0;
        auto context = GetLinuxApplicationContext();
        Window winRoot = 0, winParent = 0;
        Window* winChildren = nullptr;
        unsigned int numChilren = 0;
        XQueryTree(context->xdisplay, handle, &winRoot, &winParent, &winChildren, &numChilren);
        int x, y;
        unsigned w, h, borderWidth, depth;
        XGetGeometry(context->xdisplay, handle, &winRoot, &x, &y, &w, &h, &borderWidth, &depth);
        return h;
    }
    void LinuxSystemWindow::CenterScreen()
    {
        auto context = GetLinuxApplicationContext();
        XWindowAttributes attributes;
        XGetWindowAttributes(context->xdisplay, handle, &attributes);
        int screenWidth = WidthOfScreen(attributes.screen);
        int screenHeight = HeightOfScreen(attributes.screen);
        int x = (screenWidth - currentWidth) / 2;
        int y = (screenHeight - currentHeight) / 2;
        XMoveWindow(context->xdisplay, handle, x, y);
    }
    void LinuxSystemWindow::Close()
    {
        if (handle)
        {
            uiContext = nullptr;
            auto context = GetLinuxApplicationContext();
            context->systemWindows.Remove(handle);
            XDestroyWindow(context->xdisplay, handle);
            handle = 0;
            visible = false;
        }
    }
    bool LinuxSystemWindow::Focused()
    {
        if (!handle) return false;
        auto context = GetLinuxApplicationContext();
        int revertTo;
        Window focusedWindow;
        XGetInputFocus(context->xdisplay, &focusedWindow, &revertTo);
        return focusedWindow == handle;
    }
    WindowHandle LinuxSystemWindow::GetNativeHandle()
    {
        WindowHandle rs;
        auto context = GetLinuxApplicationContext();
        rs.window = handle;
        rs.display = context->xdisplay;
        return rs;
    }
    void LinuxSystemWindow::SetText(CoreLib::String text)
    {
        if (!handle) return;
        auto context = GetLinuxApplicationContext();
        XStoreName(context->xdisplay, handle, text.Buffer());
        XClassHint* hint = XAllocClassHint();
        hint->res_class = (char *)"Game Engine";
        hint->res_name = (char *)"Game Engine";
        XSetClassHint(context->xdisplay, handle, hint);
        XFree(hint);
    }
    bool LinuxSystemWindow::IsVisible()
    {
        return visible;
    }
    void LinuxSystemWindow::Show()
    {
        XMapWindow(GetLinuxApplicationContext()->xdisplay, handle);
        visible = true;
    }
    void LinuxSystemWindow::Hide()
    {
        if (!handle) return;
        XUnmapWindow(GetLinuxApplicationContext()->xdisplay, handle);
        visible = false;
    }
    int LinuxSystemWindow::GetCurrentDpi()
    {
        if (forceDPIValue != 0)
            return forceDPIValue;
        char *resourceString = XResourceManagerString(GetLinuxApplicationContext()->xdisplay);
        XrmDatabase db;
        XrmValue value;
        char *type = NULL;
        double dpi = 96.0;
        db = XrmGetStringDatabase(resourceString);
        if (resourceString)
        {
            if (XrmGetResource(db, "Xft.dpi", "String", &type, &value))
            {
                if (value.addr)
                {
                    dpi = atof(value.addr);
                }
            }
        }
        return (int)dpi;
    }

    void LinuxSystemWindow::Invoke(const CoreLib::Event<>& f)
    {
        auto context = GetLinuxApplicationContext();
        if (std::this_thread::get_id() == context->uiThreadId)
        {
            f();
        }
    }
    void LinuxSystemWindow::InvokeAsync(const CoreLib::Event<>& f)
    {
        auto context = GetLinuxApplicationContext();
        context->QueueTask(f, this);
    }

    DialogResult LinuxSystemWindow::ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags)
    {
        auto context = GetLinuxApplicationContext();
        if (!context->msgboxWindow)
            context->msgboxWindow = new MessageBoxWindow();
        context->msgboxWindow->Config(msg, title, flags);
        return context->msgboxWindow->Show(this);
    }

    GraphicsUI::SHIFTSTATE GetShiftState(int state)
    {
        GraphicsUI::SHIFTSTATE shiftstate = 0;
        if (state & ShiftMask) shiftstate |= GraphicsUI::SS_SHIFT;
        if (state & ControlMask) shiftstate |= GraphicsUI::SS_CONTROL;
        if (state & Mod1Mask) shiftstate |= GraphicsUI::SS_ALT;
        if (state & Button1Mask) shiftstate |= GraphicsUI::SS_BUTTONLEFT;
        if (state & Button2Mask) shiftstate |= GraphicsUI::SS_BUTTONMIDDLE;
        if (state & Button3Mask) shiftstate |= GraphicsUI::SS_BUTTONRIGHT;
        return shiftstate;
    }

    void LinuxSystemWindow::HandleKeyEvent(KeyEvent eventType, int keyCode, int keyChar, int state)
    {
        if (!isEnabled)
        {
            CheckAndRaiseModalWindow();
            return;
        }
        auto context = GetLinuxApplicationContext();
        auto shiftstate = GetShiftState(state);
        if (eventType == KeyEvent::Press)
        {
            if (context->CheckKeyState(keyCode) == KeyState::Pressed)
            {
                // This key has just been pressed, issue both key down and key press message
                this->uiContext->uiEntry->DoKeyDown(keyCode, shiftstate);
            }
            if (keyChar)
                this->uiContext->uiEntry->DoKeyPress(keyChar, shiftstate);
        }
        else
        {
            this->uiContext->uiEntry->DoKeyUp(keyCode, shiftstate);
        }
    }

    void LinuxSystemWindow::HandleMouseEvent(MouseEvent eventType, int x, int y, int delta, int button, int state, unsigned long time)
    {
        if (!isEnabled)
        {
            CheckAndRaiseModalWindow();
            return;
        }   
        
        auto shiftstate = GetShiftState(state);
        if (button == Button1)
            shiftstate |= GraphicsUI::SS_BUTTONLEFT;
        else if (button == Button2)
            shiftstate |= GraphicsUI::SS_BUTTONMIDDLE;
        else if (button == Button3)
            shiftstate |= GraphicsUI::SS_BUTTONRIGHT;

        switch (eventType)
        {
        case MouseEvent::Down:
            this->uiContext->tmrHover->Start();
            this->uiContext->uiEntry->DoMouseDown(x, y, shiftstate);
            if (x == lastMouseDownX && y == lastMouseDownY && time - lastMouseDownTime < 200)
            {
                this->uiContext->uiEntry->DoDblClick();
                lastMouseDownTime = 0;
            }
            else
            {
                lastMouseDownTime = time;
            }
            lastMouseDownX = x;
            lastMouseDownY = y;
            break;
        case MouseEvent::Up:
            this->uiContext->tmrHover->Stop();
            this->uiContext->uiEntry->DoMouseUp(x, y, shiftstate);
            break;
        case MouseEvent::Move:
            this->uiContext->tmrHover->Start();
            this->uiContext->uiEntry->DoMouseMove(x, y);
            break;
        case MouseEvent::Scroll:
            this->uiContext->uiEntry->DoMouseWheel(delta, shiftstate);
            break;
        }
        cursorX = x;
        cursorY = y;
    }

    void LinuxSystemWindow::HandleResizeEvent(int w, int h)
    {
        if (w != currentWidth || h != currentHeight)
        {
            currentWidth = w;
            currentHeight = h;
            uiContext->SetSize(w, h);
            SystemWindow::SizeChanged();
            CheckAndRaiseModalWindow();
        }
    }

    void LinuxSystemWindow::HandleCloseEvent()
    {
        if (isEnabled)
        {
            Hide();
            auto context = GetLinuxApplicationContext();
            if (context->mainWindow == this)
                context->terminate = true;
        }
        else
        {
            CheckAndRaiseModalWindow();
        }
    }

    void LinuxSystemWindow::HandleExposeEvent()
    {
        CheckAndRaiseModalWindow();
    }

    void LinuxSystemWindow::HandleFocus(bool focus)
    {
        if (!focus)
        {
            CheckAndRaiseModalWindow();
            auto context = GetLinuxApplicationContext();
            if (context->GetModalWindow() == this)
            {
                XSetInputFocus(context->xdisplay, handle, 1, CurrentTime);
            }
        }
    }

    void LinuxSystemWindow::CheckAndRaiseModalWindow()
    {
        auto context = GetLinuxApplicationContext();
        auto modalWindow = context->GetModalWindow();
        if (modalWindow && modalWindow != this)
        {
            XRaiseWindow(context->xdisplay, modalWindow->handle);
        }
    }

    void LinuxSystemWindow::SetFixedSize()
    {
        auto context = GetLinuxApplicationContext();
        auto sizeHints = XAllocSizeHints();
        sizeHints->flags = PMinSize | PMaxSize;
        sizeHints->min_width = sizeHints->max_width = currentWidth;
        sizeHints->min_height = sizeHints->max_height = currentHeight;
        XSetWMNormalHints(context->xdisplay, handle, sizeHints);
        XFree(sizeHints);
    }

    void SetCurrentWindowsEnabled(bool val)
    {
        auto& appContext = *GetLinuxApplicationContext();
        if (appContext.modalWindowStack.Count())
        {
            appContext.modalWindowStack.Last()->SetEnabled(val);
        }
        else
        {
            for (auto window : appContext.systemWindows)
            {
                window.Value->SetEnabled(val);
            }
        }
    }

    DialogResult LinuxSystemWindow::ShowModal(SystemWindow* parentWindow)
    {
        auto linuxParentWindow = dynamic_cast<LinuxSystemWindow *>(parentWindow);
        auto context = GetLinuxApplicationContext();
        CORELIB_ASSERT(std::this_thread::get_id() == context->uiThreadId && "ShowModal must be called from UI thread.");
        SetCurrentWindowsEnabled(false);
        SetEnabled(true);
        if (linuxParentWindow)
           XSetTransientForHint(context->xdisplay, handle, linuxParentWindow->handle);
        context->modalWindowStack.Add(this);
        context->modalDialogResult = DialogResult::Undefined;
        Show();
        CenterScreen();
        while (context->modalDialogResult == DialogResult::Undefined && visible)
        {
            OsApplication::DoEvents();
             if (!context->terminate)
                context->mainLoopEventHandler();
        }
        context->modalWindowStack.SetSize(context->modalWindowStack.Count() - 1);
        SetCurrentWindowsEnabled(true);
        auto dlgResult = context->modalDialogResult;
        context->modalDialogResult = DialogResult::Undefined;
        return dlgResult;
    };

    void LinuxSystemWindow::SetDialogResult(GameEngine::DialogResult result)
    {
        auto context = GetLinuxApplicationContext();
        context->modalDialogResult = result;
        Hide();
    }

    SystemWindow* CreateLinuxSystemWindow(UISystemBase* sysInterface, int log2BufferSize)
    {
        return new LinuxSystemWindow(sysInterface, log2BufferSize);
    }
}
#endif