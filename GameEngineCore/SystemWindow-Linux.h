#ifndef GAME_ENGINE_SYSTEM_WINDOW_LINUX_H
#define GAME_ENGINE_SYSTEM_WINDOW_LINUX_H

#if defined(__linux__)

#include "CoreLib/LibUI/LibUI.h"
#include "UISystemBase.h"
#include "OS.h"
#include "HardwareRenderer.h"
namespace GameEngine
{
    class UISystemBase;

    class LinuxSystemWindow : public SystemWindow
    {
    private:
        CoreLib::RefPtr<UIWindowContext> uiContext;
    public:
        LinuxSystemWindow(UISystemBase* sysInterface, int log2UIBufferSize);
        ~LinuxSystemWindow();
        virtual GraphicsUI::UIEntry* GetUIEntry() override;
        virtual GraphicsUI::UIWindowContext* GetUIContext() override
        {
            return uiContext.Ptr();
        }
        virtual void SetClientWidth(int w) override;
        virtual void SetClientHeight(int h) override;
        virtual int GetClientWidth() override;
        virtual int GetClientHeight() override;
        virtual void CenterScreen() override;
        virtual void Close() override;
        virtual bool Focused() override;
        virtual WindowHandle GetNativeHandle() override;
        virtual void SetText(CoreLib::String text) override;
        virtual bool IsVisible() override;
        virtual void Show() override;
        virtual void Hide() override;
        virtual int GetCurrentDpi() override;
        virtual void Invoke(const CoreLib::Event<>& f) override;
        virtual void InvokeAsync(const CoreLib::Event<>& f) override;
        virtual GameEngine::DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags) override;
    };

}

#endif

#endif