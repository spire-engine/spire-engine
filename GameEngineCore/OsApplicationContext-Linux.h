#ifdef __linux__

#ifndef OS_APPLICATION_CONTEXT_LINUX_H
#define OS_APPLICATION_CONTEXT_LINUX_H

#include "CoreLib/Basic.h"
#include <X11/Xlib.h>
#include <thread>
#include "CoreLib/Threading.h"

namespace GameEngine
{
    class SystemWindow;

    struct UIThreadTask
    {
        CoreLib::Event<> callback;
    };

    struct LinuxApplicationContext
    {
        std::thread::id uiThreadId;
        Display* xdisplay = nullptr;
        SystemWindow* mainWindow = nullptr;
        CoreLib::EnumerableDictionary<Window, SystemWindow*> systemWindows;
        bool terminate = false;
        CoreLib::Procedure<> mainLoopEventHandler;
        CoreLib::Threading::Mutex uiThreadTaskQueueMutex;
        CoreLib::List<UIThreadTask> uiThreadTaskQueue;
        void Free()
        {
            uiThreadTaskQueue = decltype(uiThreadTaskQueue)();
            systemWindows = decltype(systemWindows)();
            mainLoopEventHandler = decltype(mainLoopEventHandler)();
        }
    };
}

#endif

#endif