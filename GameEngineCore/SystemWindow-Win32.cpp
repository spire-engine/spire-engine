#include "SystemWindow-Win32.h"
#include "UISystem-Win32.h"
#include "Engine.h"
#include "CoreLib/WinForm/WinApp.h"

namespace GameEngine
{
    using namespace GraphicsUI;
    using namespace CoreLib;
    using namespace VectorMath;
    
    Win32SystemWindow::Win32SystemWindow(UISystemBase * psysInterface, int log2UIBufferSize)
    {
        Create();
        this->wantChars = true;
        this->uiContext = psysInterface->CreateWindowContext(this, GetClientWidth(), GetClientHeight(), log2UIBufferSize);
        OnResized.Bind(this, &Win32SystemWindow::WindowResized);
        OnResizing.Bind(this, &Win32SystemWindow::WindowResizing);
    }

    Win32SystemWindow::~Win32SystemWindow()
    {
        CoreLib::WinForm::Application::UnRegisterComponent(this);
    }

    GraphicsUI::UIEntry * Win32SystemWindow::GetUIEntry()
    {
        return uiContext->uiEntry.Ptr();
    }

    int Win32SystemWindow::GetCurrentDpi()
    {
        return Win32UISystem::GetCurrentDpi(handle);
    }
   
    void Win32SystemWindow::Create()
    {
        handle = CreateWindowW(CoreLib::WinForm::Application::GLFormClassName, 0, WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, CoreLib::WinForm::Application::GetHandle(), NULL);
        if (!handle)
        {
            throw "Failed to create window.";
        }
        CoreLib::WinForm::Application::RegisterComponent(this);
        SubClass();
    }

    void Win32SystemWindow::WindowResized(CoreLib::Object * /*sender*/, CoreLib::WinForm::EventArgs /*e*/)
    {
        SystemWindow::SizeChanged();
        uiContext->SetSize(BaseForm::GetClientWidth(), BaseForm::GetClientHeight());
    }

    void Win32SystemWindow::WindowResizing(CoreLib::Object * /*sender*/, CoreLib::WinForm::ResizingEventArgs &/* e*/)
    {
        uiContext->SetSize(BaseForm::GetClientWidth(), BaseForm::GetClientHeight());

        Engine::Instance()->RefreshUI();
    }

    int Win32SystemWindow::ProcessMessage(CoreLib::WinForm::WinMessage & msg)
    {
        auto engine = Engine::Instance();
        int rs = -1;
        if (engine)
        {
            auto sysInterface = dynamic_cast<Win32UISystem*>(engine->GetUISystemInterface());
            if (sysInterface)
                rs = sysInterface->HandleSystemMessage(dynamic_cast<SystemWindow*>(this), msg.message, msg.wParam, msg.lParam);
        }
        if (rs == -1)
            return BaseForm::ProcessMessage(msg);
        return rs;
    }

}