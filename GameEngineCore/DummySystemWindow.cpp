#include "OS.h"
#include "UISystemBase.h"

namespace GameEngine
{
    class DummySystemWindow : public SystemWindow
    {
    private:
        int width = 0, height = 0;
        bool visible = false;
        CoreLib::RefPtr<UIWindowContext> uiContext;

    public:
        DummySystemWindow(UISystemBase* pSysInterface, int log2UIBufferSize)
        {
            width = 1920;
            height = 1080;
            this->uiContext = pSysInterface->CreateWindowContext(this, GetClientWidth(), GetClientHeight(), log2UIBufferSize);
        }
        virtual GraphicsUI::UIEntry * GetUIEntry() override
        {
            return uiContext->uiEntry.Ptr();
        }
        virtual GraphicsUI::UIWindowContext* GetUIContext() override
        {
            return uiContext.Ptr();
        }
        virtual void SetClientWidth(int w) override
        {
            width = w;
            this->uiContext->SetSize(width, height);
            SystemWindow::SizeChanged();
        }
        virtual void SetClientHeight(int h) override
        {
            height = h;
            this->uiContext->SetSize(width, height);
            SystemWindow::SizeChanged();
        }
        virtual int GetClientWidth() override { return width; }
        virtual int GetClientHeight() override { return height; }
        virtual void CenterScreen() override {}
        virtual void Close() override {}
        virtual bool Focused() override { return false; }
        virtual WindowHandle GetNativeHandle() override
        {
            return WindowHandle();
        }
        virtual void SetText(CoreLib::String text) override {}
        virtual bool IsVisible() override
        {
            return visible;
        }
        virtual void Show() override
        {
            visible = true;
        }
        virtual DialogResult ShowModal(SystemWindow* /*parent*/) override
        {
            return DialogResult::Cancel;
        }
        virtual void Hide() override
        {
            visible = false;
        }
        virtual void Invoke(const CoreLib::Event<> & /*func*/) override {}
        virtual void InvokeAsync(const CoreLib::Event<> & /*func*/) override {}
        virtual int GetCurrentDpi() override
        {
            return 96;
        }
        virtual DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags = MessageBoxFlags::OKOnly) override
        {
            return OsApplication::ShowMessage(msg, title, flags);
        }
    };

    SystemWindow* OsApplication::CreateDummyWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize)
    {
        return new DummySystemWindow(dynamic_cast<UISystemBase*>(sysInterface), log2BufferSize);
    }
}