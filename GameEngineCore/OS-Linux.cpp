#if defined(__linux__)

#include "OS.h"
#include "HardwareRenderer.h"
#include "UISystem-Linux.h"
#include "OsApplicationContext-Linux.h"
#include <X11/Xlib.h>

namespace GameEngine
{
    // Implemented in FontRasterizer-Generic.cpp
    OsFontRasterizer* CreateGenericFontRasterizer();
    // Implemented in SystemWindow-Linux.cpp
    SystemWindow* CreateLinuxSystemWindow(UISystemBase* sysInterface, int log2BufferSize);

    CoreLib::Text::CommandLineParser OsApplication::commandlineParser;

    static LinuxApplicationContext appContext;

    LinuxApplicationContext* GetLinuxApplicationContext()
    {
        return &appContext;
    }

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
        appContext.mainLoopEventHandler = handler;
    }
    SystemWindow* OsApplication::CreateSystemWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize)
    {
        if (!appContext.xdisplay)
        {
            appContext.xdisplay = XOpenDisplay(nullptr);
            if (!appContext.xdisplay)
                printf("Failed to open XDisplay.\n");
        }
        auto rs = CreateLinuxSystemWindow(dynamic_cast<UISystemBase*>(sysInterface), log2BufferSize);
        rs->GetUIEntry()->BackColor = GraphicsUI::Color(50, 50, 50);
        return dynamic_cast<SystemWindow*>(rs);
    }

    void OsApplication::DoEvents()
    {
        while (XPending(appContext.xdisplay))
        {
            XEvent nextEvent;
            XNextEvent(appContext.xdisplay, &nextEvent);
            switch (nextEvent.type)
            {
            case ClientMessage:
                if (appContext.mainWindow->GetNativeHandle().window == nextEvent.xclient.window)
                {
                    Atom wmDelete = XInternAtom(appContext.xdisplay, "WM_DELETE_WINDOW", True);
                    if (nextEvent.xclient.data.l[0] == wmDelete)
                        appContext.terminate = true;
                }
                break;
            }
        }
        if (appContext.uiThreadTaskQueueMutex.TryLock())
        {
            for (auto & task : appContext.uiThreadTaskQueue)
            {
                task.callback();
                task.callback = decltype(task.callback)();
            }
            appContext.uiThreadTaskQueue.Clear();
            appContext.uiThreadTaskQueueMutex.Unlock();
        }
    }

    void OsApplication::Run(SystemWindow* mainWindow)
    {
        appContext.uiThreadId = std::this_thread::get_id();
        appContext.mainWindow = mainWindow;
        mainWindow->Show();
        while (!appContext.terminate)
        {
            DoEvents();
            appContext.mainLoopEventHandler();
        }
    }
    void OsApplication::Dispose()
    {
        commandlineParser = CoreLib::Text::CommandLineParser();
        appContext.Free();
        XCloseDisplay(appContext.xdisplay);
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

    class LinuxFileDialog : public OsFileDialog
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

    OsFileDialog* OsApplication::CreateFileDialog(SystemWindow* parent)
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