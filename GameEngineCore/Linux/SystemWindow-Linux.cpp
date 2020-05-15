#if defined(__linux__)

#include "SystemWindow-Linux.h"
#include "OsApplicationContext-Linux.h"
#include <X11/Xlib.h>
#include <X11/Xresource.h>

namespace GameEngine
{
    // Defined in OS-Linux.cpp
    LinuxApplicationContext* GetLinuxApplicationContext();

    LinuxSystemWindow::LinuxSystemWindow(UISystemBase* pSysInterface, int log2UIBufferSize)
    {
        auto context = GetLinuxApplicationContext();
        int blackColor = BlackPixel(context->xdisplay, DefaultScreen(context->xdisplay));
        int whiteColor = WhitePixel(context->xdisplay, DefaultScreen(context->xdisplay));
        currentWidth = 1920;
        currentHeight = 1080;
        handle = XCreateSimpleWindow(context->xdisplay, DefaultRootWindow(context->xdisplay), 0, 0,
                                     currentWidth, currentHeight, 0, blackColor, blackColor);
        context->systemWindows[handle] = this;
        Atom wmDelete = XInternAtom(context->xdisplay, "WM_DELETE_WINDOW", True);
        XSetWMProtocols(context->xdisplay, handle, &wmDelete, 1);
        XSelectInput(context->xdisplay, handle, StructureNotifyMask | KeyPressMask | KeyReleaseMask 
            | PointerMotionMask | ButtonPressMask | ButtonReleaseMask );
        this->uiContext = pSysInterface->CreateWindowContext(this, GetClientWidth(), GetClientHeight(), log2UIBufferSize);
    }

    LinuxSystemWindow::~LinuxSystemWindow()
    {
        uiContext = nullptr;
        Close();
    }
    GraphicsUI::UIEntry* LinuxSystemWindow::GetUIEntry()
    {
        return uiContext->uiEntry.Ptr();
    }
    void LinuxSystemWindow::SetClientWidth(int w)
    {
        XResizeWindow(GetLinuxApplicationContext()->xdisplay, handle, w, GetClientHeight());
        currentWidth = w;
    }
    void LinuxSystemWindow::SetClientHeight(int h)
    {
        XResizeWindow(GetLinuxApplicationContext()->xdisplay, handle, GetClientWidth(), h);
        currentHeight = h;
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
    }
    void LinuxSystemWindow::Close()
    {
        if (handle)
        {
            auto context = GetLinuxApplicationContext();
            context->systemWindows.Remove(handle);
            XDestroyWindow(context->xdisplay, handle);
            handle = 0;
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
        context->uiThreadTaskQueueMutex.Lock();
        UIThreadTask task;
        task.callback = new CoreLib::Event<>(f);
        context->uiThreadTaskQueue.Add(CoreLib::_Move(task));
        context->uiThreadTaskQueueMutex.Unlock();
    }

    DialogResult LinuxSystemWindow::ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags)
    {
        return OsApplication::ShowMessage(msg, title, flags);
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
            this->uiContext->uiEntry->DoMouseUp(x, y, shiftstate);
            break;
        case MouseEvent::Move:
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
            printf("resize event %d %d\n", w, h);
            currentWidth = w;
            currentHeight = h;
            uiContext->SetSize(w, h);
            SystemWindow::SizeChanged();
        }
    }

    SystemWindow* CreateLinuxSystemWindow(UISystemBase* sysInterface, int log2BufferSize)
    {
        return new LinuxSystemWindow(sysInterface, log2BufferSize);
    }
}
#endif