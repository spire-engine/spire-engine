#ifdef __linux__

#ifndef OS_APPLICATION_CONTEXT_LINUX_H
#define OS_APPLICATION_CONTEXT_LINUX_H

#include "CoreLib/Basic.h"
#include <thread>
#include "CoreLib/Threading.h"
#include <X11/Xlib.h>

namespace GameEngine
{
    class LinuxSystemWindow;

    struct UIThreadTask
    {
        CoreLib::RefPtr<CoreLib::Event<>> callback;
    };

    enum class KeyState : char
    {
        Released,
        Pressed,
        Hold,
    };
    
    const int KeyStateTableSize = 256;

    struct LinuxApplicationContext
    {
        std::thread::id uiThreadId;
        Display* xdisplay = nullptr;
        SystemWindow* mainWindow = nullptr;
        SystemWindow* currentMouseEventWindow = nullptr;
        CoreLib::EnumerableDictionary<Window, LinuxSystemWindow*> systemWindows;
        KeyState keyStates[KeyStateTableSize] = {};
        bool terminate = false;
        CoreLib::Procedure<> mainLoopEventHandler;
        CoreLib::Threading::Mutex uiThreadTaskQueueMutex;
        CoreLib::List<UIThreadTask> uiThreadTaskQueue;
        CoreLib::List<Cursor> cursors;
        void Free()
        {
            uiThreadTaskQueue = decltype(uiThreadTaskQueue)();
            systemWindows = decltype(systemWindows)();
            mainLoopEventHandler = decltype(mainLoopEventHandler)();
            cursors = decltype(cursors)();
        }
        KeyState CheckKeyState(int keyCode)
        {
            if (keyCode < KeyStateTableSize)
                return keyStates[keyCode];
            else
                return KeyState::Released;
        }
    };
}

#endif

#endif