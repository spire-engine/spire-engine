#if defined(__linux__)

#include "SystemWindow-Linux.h"
#include "OsApplicationContext-Linux.h"

namespace GameEngine
{
    // Defined in OS-Linux.cpp
    LinuxApplicationContext* GetLinuxApplicationContext();

    LinuxSystemWindow::LinuxSystemWindow(UISystemBase* pSysInterface, int log2UIBufferSize)
    {
        auto context = GetLinuxApplicationContext();
        int blackColor = BlackPixel(context->xdisplay, DefaultScreen(context->xdisplay));
        int whiteColor = WhitePixel(context->xdisplay, DefaultScreen(context->xdisplay));
        handle = XCreateSimpleWindow(context->xdisplay, DefaultRootWindow(context->xdisplay), 0, 0,
                                     1920, 1080, 0, blackColor, blackColor);
        context->systemWindows[handle] = this;
        Atom wmDelete = XInternAtom(context->xdisplay, "WM_DELETE_WINDOW", True);
        XSetWMProtocols(context->xdisplay, handle, &wmDelete, 1);
        XSelectInput(context->xdisplay, handle, StructureNotifyMask);
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
        XResizeWindow(GetLinuxApplicationContext()->xdisplay, handle, w, GetClientHeight());
    }
    void LinuxSystemWindow::SetClientHeight(int h)
    {
        XResizeWindow(GetLinuxApplicationContext()->xdisplay, handle, GetClientWidth(), h);
    }
    int LinuxSystemWindow::GetClientWidth()
    {
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
        XUnmapWindow(GetLinuxApplicationContext()->xdisplay, handle);
        visible = false;
    }
    int LinuxSystemWindow::GetCurrentDpi()
    {
        return 96;
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
        task.callback = f;
        context->uiThreadTaskQueue.Add(CoreLib::_Move(task));
        context->uiThreadTaskQueueMutex.Unlock();
    }
    DialogResult LinuxSystemWindow::ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags)
    {
        return DialogResult::OK;
    }
    SystemWindow* CreateLinuxSystemWindow(UISystemBase* sysInterface, int log2BufferSize)
    {
        return new LinuxSystemWindow(sysInterface, log2BufferSize);
    }
}
#endif