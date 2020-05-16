#if defined(__linux__)

#include "OS.h"
#include "HardwareRenderer.h"
#include "Linux/UISystem-Linux.h"
#include "Linux/OsApplicationContext-Linux.h"
#include "Linux/SystemWindow-Linux.h"
#include "CoreLib/Tokenizer.h"
#include "Linux/tinyfiledialogs.h"
#include <signal.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>

namespace GameEngine
{
    // Implemented in FontRasterizer-Generic.cpp
    OsFontRasterizer* CreateGenericFontRasterizer();
    // Implemented in SystemWindow-Linux.cpp
    SystemWindow* CreateLinuxSystemWindow(UISystemBase* sysInterface, int log2BufferSize);
    // Implemented in X11KeyCodeTranslater.cpp
    void InitKeyCodeTranslationTable(Display* display);
    void FreeKeyCodeTranslationTable();
    int TranslateKeyCode(int keyCode);
    int GetKeyChar(int keyCode, int keyState);

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

    void InitCursors()
    {
        unsigned int xcursorShape = 0;
        appContext.cursors.SetSize((int)GraphicsUI::CursorType::_Count);
        for (int i = 0; i < appContext.cursors.Count(); i++)
        {
            auto cursorType = static_cast<GraphicsUI::CursorType>(i);
            switch (cursorType)
            {
            case GraphicsUI::CursorType::Arrow:
                xcursorShape = XC_arrow;
                break;
            case GraphicsUI::CursorType::Cross:
                xcursorShape = XC_cross;
                break;
            case GraphicsUI::CursorType::IBeam:
                xcursorShape = XC_xterm;
                break;
            case GraphicsUI::CursorType::Wait:
                xcursorShape = XC_clock;
                break;
            case GraphicsUI::CursorType::SizeNS:
                xcursorShape = XC_sb_v_double_arrow;
                break;
            case GraphicsUI::CursorType::SizeWE:
                xcursorShape = XC_sb_h_double_arrow;
                break;
            case GraphicsUI::CursorType::SizeNESW_Top:
                xcursorShape = XC_top_right_corner;
                break;
            case GraphicsUI::CursorType::SizeNESW_Bottom:
                xcursorShape = XC_bottom_left_corner;
                break;
            case GraphicsUI::CursorType::SizeNWSE_Top:
                xcursorShape = XC_top_left_corner;
                break;
            case GraphicsUI::CursorType::SizeNWSE_Bottom:
                xcursorShape = XC_bottom_right_corner;
                break;
            case GraphicsUI::CursorType::SizeAll:
            default:
                xcursorShape = XC_fleur;
                break;
            }
            appContext.cursors[i] = XCreateFontCursor(appContext.xdisplay, xcursorShape);
        }
    }

    void OsApplication::Quit()
    {
        appContext.terminate = true;
    }

    SystemWindow* OsApplication::CreateSystemWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize)
    {
        if (!appContext.xdisplay)
        {
            XInitThreads();
            XrmInitialize();
            appContext.xdisplay = XOpenDisplay(nullptr);
            InitKeyCodeTranslationTable(appContext.xdisplay);
            InitCursors();
            if (!appContext.xdisplay)
                printf("Failed to open XDisplay.\n");
        }
        auto rs = CreateLinuxSystemWindow(dynamic_cast<UISystemBase*>(sysInterface), log2BufferSize);
        rs->GetUIEntry()->BackColor = GraphicsUI::Color(50, 50, 50);
        return dynamic_cast<SystemWindow*>(rs);
    }

    void OsApplication::DoEvents()
    {
        if (!appContext.xdisplay)
            return;
        static bool supressInvokeTasks = false;
        LinuxSystemWindow* sysWindow = nullptr;
        int vKeyCode = 0;
        while (XPending(appContext.xdisplay))
        {
            XEvent nextEvent;
            XNextEvent(appContext.xdisplay, &nextEvent);
            switch (nextEvent.type)
            {
            case KeyPress:
                vKeyCode = TranslateKeyCode(nextEvent.xkey.keycode);
                if (vKeyCode < KeyStateTableSize)
                {
                    if (appContext.keyStates[vKeyCode] == KeyState::Released)
                        appContext.keyStates[vKeyCode] = KeyState::Pressed;
                    else if (appContext.keyStates[vKeyCode] == KeyState::Pressed) 
                        appContext.keyStates[vKeyCode] = KeyState::Hold;
                }
                if (appContext.systemWindows.TryGetValue(nextEvent.xkey.window, sysWindow))
                {
                    wchar_t keyChar = GetKeyChar(vKeyCode, nextEvent.xkey.state);
                    sysWindow->HandleKeyEvent(KeyEvent::Press, vKeyCode, keyChar, nextEvent.xkey.state);
                }
                break;
            case KeyRelease:
                vKeyCode = TranslateKeyCode(nextEvent.xkey.keycode);
                if (vKeyCode < KeyStateTableSize)
                {
                    appContext.keyStates[vKeyCode] = KeyState::Released;
                }
                if (appContext.systemWindows.TryGetValue(nextEvent.xkey.window, sysWindow))
                {
                    sysWindow->HandleKeyEvent(KeyEvent::Release, vKeyCode, 0, nextEvent.xkey.state);
                }
                break;
            case MotionNotify:
                if (appContext.systemWindows.TryGetValue(nextEvent.xmotion.window, sysWindow))
                {
                    appContext.currentMouseEventWindow = sysWindow;
                    sysWindow->HandleMouseEvent(MouseEvent::Move, nextEvent.xmotion.x, nextEvent.xmotion.y, 0, 
                        0, nextEvent.xmotion.state, nextEvent.xmotion.time);
                }
                break;
            case ButtonPress:
                if (appContext.systemWindows.TryGetValue(nextEvent.xbutton.window, sysWindow))
                {
                    appContext.currentMouseEventWindow = sysWindow;
                    if (nextEvent.xbutton.button <= Button3)
                        sysWindow->HandleMouseEvent(MouseEvent::Down, nextEvent.xbutton.x, nextEvent.xbutton.y, 0,
                            nextEvent.xbutton.button, nextEvent.xbutton.state, nextEvent.xbutton.time);
                    else if (nextEvent.xbutton.button == Button4)
                        sysWindow->HandleMouseEvent(MouseEvent::Scroll, nextEvent.xbutton.x, nextEvent.xbutton.y, 120,
                            nextEvent.xbutton.button, nextEvent.xbutton.state, nextEvent.xbutton.time);
                    else if (nextEvent.xbutton.button == Button5)
                        sysWindow->HandleMouseEvent(MouseEvent::Scroll, nextEvent.xbutton.x, nextEvent.xbutton.y, -120, 
                            nextEvent.xbutton.button, nextEvent.xbutton.state, nextEvent.xbutton.time);
                }
                break;
            case ButtonRelease:
                if (appContext.systemWindows.TryGetValue(nextEvent.xbutton.window, sysWindow))
                {
                    appContext.currentMouseEventWindow = sysWindow;
                    sysWindow->HandleMouseEvent(MouseEvent::Up, nextEvent.xbutton.x, nextEvent.xbutton.y, 0,
                        nextEvent.xbutton.button, nextEvent.xbutton.state, nextEvent.xbutton.time);
                }
                break;
            case ConfigureNotify:
                if (appContext.systemWindows.TryGetValue(nextEvent.xconfigure.window, sysWindow))
                {
                    sysWindow->HandleResizeEvent(nextEvent.xconfigure.width, nextEvent.xconfigure.height);
                }
                break;
            case ClientMessage:
                if (appContext.mainWindow->GetNativeHandle().window == nextEvent.xclient.window)
                {
                    Atom wmDelete = XInternAtom(appContext.xdisplay, "WM_DELETE_WINDOW", True);
                    if (nextEvent.xclient.data.l[0] == wmDelete)
                        appContext.terminate = true;
                }
                if (appContext.systemWindows.TryGetValue(nextEvent.xbutton.window, sysWindow))
                {
                    Atom wmDelete = XInternAtom(appContext.xdisplay, "WM_DELETE_WINDOW", True);
                    if (nextEvent.xclient.data.l[0] == wmDelete)
                    {
                        sysWindow->Hide();
                    }
                }
                break;
            }
        }
        if (!supressInvokeTasks)
        {
            supressInvokeTasks = true;
            if (appContext.uiThreadTaskQueueMutex.TryLock())
            {
                for (auto & task : appContext.uiThreadTaskQueue)
                {
                    task.callback->Invoke();
                    task.callback = nullptr;
                }
                appContext.uiThreadTaskQueue.Clear();
                appContext.uiThreadTaskQueueMutex.Unlock();
            }
            supressInvokeTasks = false;
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
            if (!appContext.terminate)
                appContext.mainLoopEventHandler();
        }
    }
    void OsApplication::Dispose()
    {
        commandlineParser = CoreLib::Text::CommandLineParser();
        FreeKeyCodeTranslationTable();
        for (auto cursor : appContext.cursors)
            XFreeCursor(appContext.xdisplay, cursor);
#if 0
        if (appContext.xdisplay)
            XCloseDisplay(appContext.xdisplay);
#endif
        appContext.Free();
    }
    void OsApplication::DebugPrint(const char* buffer)
    {
#ifdef _DEBUG
        printf("%s", buffer);
#endif
    }
    GameEngine::DialogResult OsApplication::ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags)
    {
        int dialogResult = 0;
        switch (flags)
        {
        case MessageBoxFlags::OKOnly:
            tinyfd_messageBox(title.Buffer(), msg.Buffer(), "ok", "warning", 0);
            return GameEngine::DialogResult::OK;
        case MessageBoxFlags::OKCancel:
            dialogResult = tinyfd_messageBox(title.Buffer(), msg.Buffer(), "okcancel", "warning", 0);
            return dialogResult == 0 ? GameEngine::DialogResult::Cancel : GameEngine::DialogResult::OK;
        case MessageBoxFlags::YesNo:
            dialogResult = tinyfd_messageBox(title.Buffer(), msg.Buffer(), "yesno", "warning", 0);
            return dialogResult == 1 ? DialogResult::Yes : DialogResult::No;
        case MessageBoxFlags::YesNoCancel:
            dialogResult = tinyfd_messageBox(title.Buffer(), msg.Buffer(), "yesnocancel", "warning", 0);
            if (dialogResult == 0)
                return DialogResult::Cancel;
            else if (dialogResult == 1)
                return DialogResult::Yes;
            else
                return DialogResult::No;
        default:
            return GameEngine::DialogResult::OK;
        }
    }

    class LinuxOsTimer : public OsTimer
    {
    private:
        int interval = 0;
        timer_t timerid = 0;

    public:
        LinuxOsTimer()
        {
        }
        static void SignalHandler(sigval_t sigVal)
        {
            auto timer = (LinuxOsTimer *)sigVal.sival_ptr;
            appContext.QueueTask(timer->Tick);
        }
        virtual void SetInterval(int val) override
        {
            interval = val;
        }
        virtual void Start() override
        {
            if (timerid != 0)
                Stop();
            sigevent_t sev = {};
            sev.sigev_notify = SIGEV_THREAD;
            sev.sigev_value.sival_ptr = this;
            sev._sigev_un._sigev_thread._attribute = nullptr;
            sev._sigev_un._sigev_thread._function = SignalHandler;
            timer_create(CLOCK_REALTIME, &sev, &timerid);
            struct itimerspec its;
            long long freq_nanosecs = interval * 1000000;
            its.it_value.tv_sec = freq_nanosecs / 1000000000;
            its.it_value.tv_nsec = freq_nanosecs % 1000000000;
            its.it_interval.tv_sec = its.it_value.tv_sec;
            its.it_interval.tv_nsec = its.it_value.tv_nsec;
            timer_settime(timerid, 0, &its, nullptr);
        }
        virtual void Stop() override
        {
            if (timerid != 0)
            {
                timer_delete(timerid);
                timerid = 0;
            }
        }
    };

    class LinuxFileDialog : public OsFileDialog
    {
    public:
        virtual bool ShowOpen() override
        {
            auto patterns = CoreLib::Text::Split(this->Filter, '|');
            CoreLib::List<const char*> patternList;
            for (auto & pattern : patterns)
            {
                if (pattern.StartsWith("*."))
                    patternList.Add(pattern.Buffer());
            }
            auto selectResult = tinyfd_openFileDialog("Open", this->FileName.Buffer(), patternList.Count(), patternList.Buffer(),
                nullptr, this->MultiSelect ? 1 : 0);
            FileName = "";
            FileNames.Clear();
            if (selectResult)
            {
                FileNames = CoreLib::Text::Split(selectResult, '|');
                if (FileNames.Count())
                    FileName = FileNames[0];
                return true;
            }
            return false;
        }
        virtual bool ShowSave() override
        {
            auto patterns = CoreLib::Text::Split(this->Filter, '|');
            CoreLib::List<const char*> patternList;
            for (auto & pattern : patterns)
            {
                if (pattern.StartsWith("*."))
                    patternList.Add(pattern.Buffer());
            }
            auto selectResult = tinyfd_saveFileDialog("Save", this->FileName.Buffer(), patternList.Count(), patternList.Buffer(),
                nullptr);
            FileName = "";
            FileNames.Clear();
            if (selectResult)
            {
                FileName = selectResult;
                return true;
            }
            return false;
        }
        LinuxFileDialog(const SystemWindow* /*_owner*/)
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