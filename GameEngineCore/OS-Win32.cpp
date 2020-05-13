#ifdef _WIN32

#include <Windows.h>
#include "CoreLib/WinForm/WinCommonDlg.h"
#include "OS.h"
#include "UISystem-Win32.h"
#include "SystemWindow-Win32.h"
#include "CoreLib/WinForm/WinApp.h"
#include "CoreLib/WinForm/Debug.h"
#include "CoreLib/WinForm/WinTimer.h"
#include "HardwareRenderer.h"

namespace GameEngine
{
    // Implemented in FontRasterizer-Win32.cpp
    OsFontRasterizer* CreateWin32FontRasterizer();
    OsFontRasterizer* CreateGenericFontRasterizer();

    CoreLib::Text::CommandLineParser OsApplication::commandlineParser;

    void OsApplication::Init(int /*argc*/, const char** /*argv*/)
    {
        WinForm::Application::Init();
        commandlineParser.Parse(WinForm::Application::GetCommandLine());
    }
    GraphicsUI::ISystemInterface* OsApplication::CreateUISystemInterface(HardwareRenderer * renderer)
    {
        return new Win32UISystem(renderer);
    }
    void OsApplication::SetMainLoopEventHandler(CoreLib::Procedure<> handler)
    {
        WinForm::Application::SetMainLoopEventHandler(new WinForm::NotifyEvent([=](auto, auto) {handler(); }));
    }
    SystemWindow* OsApplication::CreateSystemWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize)
    {
        auto rs = new Win32SystemWindow(dynamic_cast<UISystemBase*>(sysInterface), log2BufferSize);
        rs->GetUIEntry()->BackColor = GraphicsUI::Color(50, 50, 50);
        return dynamic_cast<SystemWindow*>(rs);
    }

    void OsApplication::DoEvents()
    {
        WinForm::Application::DoEvents();
    }

    void OsApplication::Run(SystemWindow* mainWindow)
    {
        WinForm::Application::Run(dynamic_cast<Win32SystemWindow*>(mainWindow), true);
    }
    void OsApplication::Dispose()
    {
        WinForm::Application::Dispose();
    }
    void OsApplication::DebugPrint(const char * buffer)
    {
        CoreLib::Diagnostics::Debug::Write(buffer);
    }
    GameEngine::DialogResult OsApplication::ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags)
    {
        return GetWin32MsgBoxResult(MessageBoxW(NULL, msg.ToWString(), title.ToWString(), GetWin32MsgBoxFlags(flags)));
    }

    class OsTimerImpl : public OsTimer
    {
    public:
        CoreLib::WinForm::Timer timer;
        void OnTick(CoreLib::Object *, CoreLib::WinForm::EventArgs)
        {
            Tick();
        }
        OsTimerImpl()
        {
            timer.OnTick.Bind(this, &OsTimerImpl::OnTick);
        }
        virtual void SetInterval(int val) override
        {
            timer.Interval = val;
        }
        virtual void Start() override
        {
            timer.StartTimer();
        }
        virtual void Stop() override
        {
            timer.StopTimer();
        }
    };

    class Win32FileDialog : public FileDialog
    {
    private:
        CoreLib::WinForm::FileDialog dlg;
    public:
        virtual bool ShowOpen() override
        {
            dlg.MultiSelect = MultiSelect;
            dlg.DefaultEXT = DefaultEXT;
            dlg.CreatePrompt = CreatePrompt;
            dlg.FileMustExist = FileMustExist;
            dlg.Filter = Filter;
            dlg.HideReadOnly = HideReadOnly;
            dlg.PathMustExist = PathMustExist;
            dlg.OverwritePrompt = OverwritePrompt;
            dlg.FileName = FileName;
            bool rs = dlg.ShowOpen();
            FileName = dlg.FileName;
            FileNames = dlg.FileNames;
            return rs;
        }
        virtual bool ShowSave() override
        {
            dlg.MultiSelect = MultiSelect;
            dlg.DefaultEXT = DefaultEXT;
            dlg.CreatePrompt = CreatePrompt;
            dlg.FileMustExist = FileMustExist;
            dlg.Filter = Filter;
            dlg.HideReadOnly = HideReadOnly;
            dlg.PathMustExist = PathMustExist;
            dlg.OverwritePrompt = OverwritePrompt;
            dlg.FileName = FileName;
            bool rs = dlg.ShowSave();
            FileName = dlg.FileName;
            FileNames = dlg.FileNames;
            return rs;
        }
        Win32FileDialog(const SystemWindow * _owner)
            : dlg(dynamic_cast<Win32SystemWindow*>((SystemWindow *)_owner))
        {
        }
    };

    FileDialog* OsApplication::CreateFileDialog(SystemWindow* parent)
    {
        return new Win32FileDialog(parent);
    }

    OsTimer* OsApplication::CreateTimer()
    {
        return new OsTimerImpl();
    }

    OsFontRasterizer* OsApplication::CreateFontRasterizer()
    {
        return CreateWin32FontRasterizer();
    }

    Font::Font()
    {
        NONCLIENTMETRICS NonClientMetrics;
        NonClientMetrics.cbSize = sizeof(NONCLIENTMETRICS) - sizeof(NonClientMetrics.iPaddedBorderWidth);
        SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &NonClientMetrics, 0);
        FontName = CoreLib::String::FromWString(NonClientMetrics.lfMessageFont.lfFaceName);
        Size = 9;
        Bold = false;
        Underline = false;
        Italic = false;
        StrikeOut = false;
    }
    
}
#endif