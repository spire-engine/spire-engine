#if defined(__linux__)

#include "OS.h"
#include "HardwareRenderer.h"
#include "UISystem-Linux.h"

namespace GameEngine
{
    // Implemented in FontRasterizer-Generic.cpp
    OsFontRasterizer* CreateGenericFontRasterizer();
    // Implemented in SystemWindow-Linux.cpp
    SystemWindow* CreateLinuxSystemWindow(UISystemBase* sysInterface, int log2BufferSize);

    CoreLib::Text::CommandLineParser OsApplication::commandlineParser;

    void OsApplication::Init(int argc, const char** argv)
    {
        commandlineParser.SetArguments(argc, argv);
    }
    GraphicsUI::ISystemInterface* OsApplication::CreateUISystemInterface(HardwareRenderer* renderer)
    {
        return new LinuxUISystem(renderer);
    }
    void OsApplication::SetMainLoopEventHandler(CoreLib::Procedure<> handler)
    {
    }
    SystemWindow* OsApplication::CreateSystemWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize)
    {
        auto rs = CreateLinuxSystemWindow(dynamic_cast<UISystemBase*>(sysInterface), log2BufferSize);
        rs->GetUIEntry()->BackColor = GraphicsUI::Color(50, 50, 50);
        return dynamic_cast<SystemWindow*>(rs);
    }

    void OsApplication::DoEvents()
    {
    }

    void OsApplication::Run(SystemWindow* mainWindow)
    {
    }
    void OsApplication::Dispose()
    {
    }
    void OsApplication::DebugPrint(const char* buffer)
    {
#ifdef _DEBUG
        printf("%s", buffer);
#endif
    }
    GameEngine::DialogResult OsApplication::ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags)
    {
        return GameEngine::DialogResult::OK;
    }

    class LinuxOsTimer : public OsTimer
    {
    public:
        LinuxOsTimer()
        {
        }
        virtual void SetInterval(int val) override
        {
        }
        virtual void Start() override
        {
        }
        virtual void Stop() override
        {
        }
    };

    class LinuxFileDialog : public FileDialog
    {
    public:
        virtual bool ShowOpen() override
        {
            return false;
        }
        virtual bool ShowSave() override
        {
            return false;
        }
        LinuxFileDialog(const SystemWindow* _owner)
        {
        }
    };

    FileDialog* OsApplication::CreateFileDialog(SystemWindow* parent)
    {
        return new LinuxFileDialog(parent);
    }

    OsTimer* OsApplication::CreateTimer()
    {
        return new LinuxOsTimer();
    }

    OsFontRasterizer* OsApplication::CreateFontRasterizer()
    {
        return CreateGenericFontRasterizer();
    }

    Font::Font()
    {
        FontName = "OpenSans/OpenSans-Regular.ttf";
        Size = 9;
        Bold = false;
        Underline = false;
        Italic = false;
        StrikeOut = false;
    }

}
#endif