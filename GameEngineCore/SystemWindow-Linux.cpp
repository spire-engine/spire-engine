#if defined(__linux__)

#include "SystemWindow-Linux.h"

namespace GameEngine
{
    LinuxSystemWindow::LinuxSystemWindow(UISystemBase* sysInterface, int log2UIBufferSize)
    {
    }
    LinuxSystemWindow::~LinuxSystemWindow()
    {
    }
    GraphicsUI::UIEntry* LinuxSystemWindow::GetUIEntry()
    {
        return nullptr;
    }
    void LinuxSystemWindow::SetClientWidth(int w)
    {
    }
    void LinuxSystemWindow::SetClientHeight(int h)
    {
    }
    int LinuxSystemWindow::GetClientWidth()
    {
        return 0;
    }
    int LinuxSystemWindow::GetClientHeight()
    {
        return 0;
    }
    void LinuxSystemWindow::CenterScreen()
    {
    }
    void LinuxSystemWindow::Close()
    {
    }
    bool LinuxSystemWindow::Focused()
    {
        return false;
    }
    WindowHandle LinuxSystemWindow::GetNativeHandle()
    {
        return WindowHandle();
    }
    void LinuxSystemWindow::SetText(CoreLib::String text)
    {
    }
    bool LinuxSystemWindow::IsVisible()
    {
        return false;
    }
    void LinuxSystemWindow::Show()
    {
    }
    void LinuxSystemWindow::Hide()
    {
    }
    int LinuxSystemWindow::GetCurrentDpi()
    {
        return 96;
    }
    void LinuxSystemWindow::Invoke(const CoreLib::Event<>& f)
    {
    }
    void LinuxSystemWindow::InvokeAsync(const CoreLib::Event<>& f)
    {
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