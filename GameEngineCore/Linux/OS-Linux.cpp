#if defined(__linux__)

#include "OS.h"
#include "HardwareRenderer.h"
#include "Engine.h"
#include "Linux/UISystem-Linux.h"
#include "Linux/OsApplicationContext-Linux.h"
#include "Linux/SystemWindow-Linux.h"
#include "CoreLib/Tokenizer.h"
#include "Linux/MessageBoxWindow-Linux.h"
#include "Linux/FileDialog-Linux.h"
#include <time.h>
#include <future>
#include <sys/timerfd.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
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

    void InitClipboard()
    {
        auto display = appContext.xdisplay;
        unsigned long color = BlackPixel(display, DefaultScreen(display));
        XSetWindowAttributes wa;
        wa.event_mask = PropertyChangeMask;
        appContext.clipboardWindow = XCreateWindow(display, DefaultRootWindow(display),
                            0, 0, 1, 1, 0, 0,
                            InputOnly,
                            DefaultVisual(display, DefaultScreen(display)),
                            CWEventMask, &wa);
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
            InitClipboard();
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
            case Expose:
                if (appContext.systemWindows.TryGetValue(nextEvent.xexpose.window, sysWindow))
                {
                    sysWindow->HandleExposeEvent();
                }
                break;
            case ClientMessage:
                if (appContext.systemWindows.TryGetValue(nextEvent.xclient.window, sysWindow))
                {
                    Atom wmDelete = XInternAtom(appContext.xdisplay, "WM_DELETE_WINDOW", True);
                    if (nextEvent.xclient.data.l[0] == wmDelete)
                    {
                        sysWindow->HandleCloseEvent();
                    }
                }
                break;
            case FocusIn:
                if (appContext.systemWindows.TryGetValue(nextEvent.xfocus.window, sysWindow))
                {
                    sysWindow->HandleFocus(true);
                }
                break;
            case FocusOut:
                if (appContext.systemWindows.TryGetValue(nextEvent.xfocus.window, sysWindow))
                {
                    sysWindow->HandleFocus(false);
                }
                break;
            case SelectionRequest:
                {
                    Window owner = nextEvent.xselectionrequest.owner;
                    Atom selection = nextEvent.xselectionrequest.selection;
                    Atom target = nextEvent.xselectionrequest.target;
                    Atom property = nextEvent.xselectionrequest.property;
                    Window requestor = nextEvent.xselectionrequest.requestor;
                    Time timestamp = nextEvent.xselectionrequest.time;
                    Display *disp = nextEvent.xselection.display;

                    XEvent s;
                    s.xselection.type      = SelectionNotify;
                    s.xselection.requestor = requestor;
                    s.xselection.selection = selection;
                    s.xselection.target    = target;
                    s.xselection.property  = None;
                    s.xselection.time      = timestamp;
                    auto XA_text = XInternAtom(appContext.xdisplay, "TEXT", 0);
                    if (appContext.clipboardString.Length() && (target == XA_STRING || target == XA_text))
                    {
                        s.xselection.property = property;
                        XChangeProperty(disp, requestor, property, target, 8, PropModeReplace,
                                        reinterpret_cast<const unsigned char*>(appContext.clipboardString.Buffer()), appContext.clipboardString.Length());
                    }
                    XSendEvent(disp, nextEvent.xselectionrequest.requestor, True, 0, &s);
                }
                break;
            case SelectionNotify:
                {
                    Atom clipboardAtom = XInternAtom(appContext.xdisplay, "CLIPBOARD", False);
                    if (nextEvent.xselection.selection == clipboardAtom)
                    {
                        if (nextEvent.xselection.property)
                        {
                            Atom formatAtom = XInternAtom(appContext.xdisplay, "UTF8_STRING", False);
                            Atom propertyAtom = XInternAtom(appContext.xdisplay, "XSEL_DATA", False);
                            Atom incrAtom = XInternAtom(appContext.xdisplay, "INCR", False);
                            char *result;
                            unsigned long ressize, restail;
                            int resbits;
                            XGetWindowProperty(appContext.xdisplay, appContext.clipboardWindow, propertyAtom, 0, 1<<27, False, AnyPropertyType,
                                &incrAtom, &resbits, &ressize, &restail, (unsigned char**)&result);

                            if (formatAtom == incrAtom)
                                appContext.clipboardString = result;

                            XFree(result);
                        }
                        appContext.clipboardStringReady = true;
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
                    if (!task.cancelled)
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
        if (appContext.clipboardWindow)
            XDestroyWindow(appContext.xdisplay, appContext.clipboardWindow);
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
        if (!Engine::Instance()->IsRunning())
        {
            // Print the message to console to std out if engine has not been initialized.
            Engine::Print("%s\n", msg.Buffer());
            if (flags == MessageBoxFlags::OKOnly)
                return DialogResult::OK;
            CORELIB_UNREACHABLE("Unsupported MessageBoxFlag.");
        }
        else
        {
            CoreLib::RefPtr<MessageBoxWindow> messageBoxWindow = new MessageBoxWindow(msg, title, flags);
            return messageBoxWindow->Show(Engine::Instance()->GetMainWindow());
        }
    }

    class LinuxOsTimer : public OsTimer
    {
    private:
        int interval = 0;
        int timerfd = 0;
        std::atomic<bool> cancelled;
        std::thread timerThread;

        void TimerThreadFunc()
        {
            uint64_t val = 0;
            while (!cancelled && read(timerfd, &val, sizeof(val)) == sizeof(val))
            {
                if (val && !cancelled)
                    appContext.QueueTask(Tick, this);
            }
        }
    public:
        LinuxOsTimer()
        {
            cancelled = false;
            timerfd = timerfd_create(CLOCK_MONOTONIC, 0);
            timerThread = std::thread([this](){ TimerThreadFunc();});
        }
        ~LinuxOsTimer()
        {
            cancelled = true;
            struct itimerspec its;
            its.it_value.tv_sec = 0;
            its.it_value.tv_nsec = 1;
            its.it_interval.tv_sec = 0;
            its.it_interval.tv_nsec = 0;
            timerfd_settime(timerfd, 0, &its, nullptr);
            timerThread.join();
            // Cancell all in-flight tasks
            if (std::this_thread::get_id() != appContext.uiThreadId)
                appContext.uiThreadTaskQueueMutex.Lock();
            for (auto& task : appContext.uiThreadTaskQueue)
            {
                if (task.ownerId == this)
                    task.cancelled = true;
            }
            if (std::this_thread::get_id() != appContext.uiThreadId)
                appContext.uiThreadTaskQueueMutex.Unlock();
        }
        virtual void SetInterval(int val) override
        {
            interval = val;
        }
        virtual void Start() override
        {
            struct itimerspec its;
            long long freq_nanosecs = interval * 1000000;
            its.it_value.tv_sec = freq_nanosecs / 1000000000;
            its.it_value.tv_nsec = freq_nanosecs % 1000000000;
            its.it_interval.tv_sec = its.it_value.tv_sec;
            its.it_interval.tv_nsec = its.it_value.tv_nsec;
            timerfd_settime(timerfd, 0, &its, nullptr);
        }
        virtual void Stop() override
        {
            struct itimerspec its;
            its.it_value.tv_sec = 0;
            its.it_value.tv_nsec = 0;
            its.it_interval.tv_sec = its.it_value.tv_sec;
            its.it_interval.tv_nsec = its.it_value.tv_nsec;
            timerfd_settime(timerfd, 0, &its, nullptr);
        }
    };

    class LinuxFileDialog : public OsFileDialog
    {
    public:
        virtual bool ShowOpen() override
        {
            CoreLib::RefPtr<FileDialogWindow> dlgWindow = new FileDialogWindow(CoreLib::IO::Path::GetDirectoryName(FileName),
                                                                               Filter, FileDialogType::Open);
            if (dlgWindow->Show(Engine::Instance()->GetMainWindow()) == DialogResult::OK)
            {
                FileName = dlgWindow->GetFileName();
                return true;
            }
            return false;
        }
        virtual bool ShowSave() override
        {
            CoreLib::RefPtr<FileDialogWindow> dlgWindow = new FileDialogWindow(CoreLib::IO::Path::GetDirectoryName(FileName),
                                                                               Filter, FileDialogType::Save);
            dlgWindow->defaultExt = DefaultEXT;
            if (dlgWindow->Show(Engine::Instance()->GetMainWindow()) == DialogResult::OK)
            {
                FileName = dlgWindow->GetFileName();
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