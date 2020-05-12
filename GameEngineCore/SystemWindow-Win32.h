#ifndef GAME_ENGINE_SYSTEM_WINDOW_H
#define GAME_ENGINE_SYSTEM_WINDOW_H

#include "CoreLib/WinForm/WinForm.h"
#include "CoreLib/LibUI/LibUI.h"
#include "UISystemBase.h"
#include "OS.h"
#include "HardwareRenderer.h"

namespace GameEngine
{
    class UISystemBase;

    inline UINT GetWin32MsgBoxFlags(MessageBoxFlags flags)
    {
        UINT style = MB_ICONEXCLAMATION;
        switch (flags)
        {
        case MessageBoxFlags::OKCancel:
            style |= MB_OKCANCEL;
            break;
        case MessageBoxFlags::YesNo:
            style |= MB_YESNO;
            break;
        case MessageBoxFlags::YesNoCancel:
            style |= MB_YESNOCANCEL;
            break;
        }
        return style;
    }

    inline GameEngine::DialogResult GetWin32MsgBoxResult(int rs)
    {
        if (rs == IDOK)
            return GameEngine::DialogResult::OK;
        else if (rs == IDCANCEL)
            return GameEngine::DialogResult::Cancel;
        else if (rs == IDYES)
            return GameEngine::DialogResult::Yes;
        else if (rs == IDNO)
            return GameEngine::DialogResult::No;
        return GameEngine::DialogResult::Undefined;
    }

    class Win32SystemWindow : public virtual CoreLib::WinForm::BaseForm, public virtual SystemWindow
    {
    private:
        CoreLib::RefPtr<UIWindowContext> uiContext;
    protected:
        virtual void Create() override;
        void WindowResized(CoreLib::Object* sender, CoreLib::WinForm::EventArgs e);
        void WindowResizing(CoreLib::Object* sender, CoreLib::WinForm::ResizingEventArgs & e);

    protected:
        int ProcessMessage(CoreLib::WinForm::WinMessage & msg) override;
    public:
        Win32SystemWindow(UISystemBase * sysInterface, int log2UIBufferSize);
        ~Win32SystemWindow();
        virtual GraphicsUI::UIEntry * GetUIEntry() override;
        virtual GraphicsUI::UIWindowContext * GetUIContext() override
        {
            return uiContext.Ptr();
        }
        virtual void SetClientWidth(int w) override
        {
            BaseForm::SetClientWidth(w);
        }
        virtual void SetClientHeight(int h) override
        {
            BaseForm::SetClientHeight(h);
        }
        virtual int GetClientWidth() override
        {
            return BaseForm::GetClientWidth();
        }
        virtual int GetClientHeight() override
        {
            return BaseForm::GetClientHeight();
        }
        virtual void CenterScreen() override
        {
            BaseForm::CenterScreen();
        }
        virtual void Close() override
        {
            BaseForm::Close();
        }
        virtual bool Focused() override
        {
            return BaseForm::Focused();
        }
        virtual WindowHandle GetNativeHandle() override
        {
            return (WindowHandle)GetHandle();
        }
        virtual void SetText(CoreLib::String text) override
        {
            BaseForm::SetText(text);
        }
        virtual bool IsVisible() override
        {
            return BaseForm::GetVisible();
        }
        virtual void Show() override
        {
            BaseForm::Show();
        }
        virtual void Hide() override
        {
            BaseForm::SetVisible(false);
        }
        virtual int GetCurrentDpi() override;
        virtual void Invoke(const Event<>& f) override
        {
            BaseForm::Invoke(f);
        }
        virtual void InvokeAsync(const Event<>& f) override
        {
            BaseForm::InvokeAsync(f);
        }
        virtual GameEngine::DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags) override
        {
            UINT style = GetWin32MsgBoxFlags(flags);
            auto rs = BaseForm::MessageBox(msg, title, style);
            return GetWin32MsgBoxResult(rs);
        }
    };
}

#endif