#ifdef __linux__

#ifndef OS_APPLICATION_CONTEXT_LINUX_H
#define OS_APPLICATION_CONTEXT_LINUX_H

#include "CoreLib/Basic.h"
#include "CoreLib/Threading.h"
#include "MessageBoxWindow-Linux.h"

#include <thread>
#include <X11/Xlib.h>

namespace GameEngine
{
    class LinuxSystemWindow;
    class MessageBoxWindow;

    struct UIThreadTask
    {
        void *ownerId = nullptr;
        bool cancelled = false;
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
        CoreLib::RefPtr<MessageBoxWindow> msgboxWindow;
        CoreLib::List<LinuxSystemWindow *> modalWindowStack;
        CoreLib::EnumerableDictionary<Window, LinuxSystemWindow *> systemWindows;
        GameEngine::DialogResult modalDialogResult = GameEngine::DialogResult::Undefined;
        KeyState keyStates[KeyStateTableSize] = {};
        bool terminate = false;
        CoreLib::Procedure<> mainLoopEventHandler;
        CoreLib::Threading::Mutex uiThreadTaskQueueMutex;
        CoreLib::List<UIThreadTask> uiThreadTaskQueue;
        CoreLib::List<Cursor> cursors;
        CoreLib::String clipboardString;
        bool clipboardStringReady = false;
        Window clipboardWindow = 0;
        LinuxSystemWindow *GetModalWindow()
        {
            if (modalWindowStack.Count())
                return modalWindowStack.Last();
            return nullptr;
        }
        void Free()
        {
            uiThreadTaskQueue = decltype(uiThreadTaskQueue)();
            systemWindows = decltype(systemWindows)();
            mainLoopEventHandler = decltype(mainLoopEventHandler)();
            cursors = decltype(cursors)();
            modalWindowStack = decltype(modalWindowStack)();
        }
        KeyState CheckKeyState(int keyCode)
        {
            if (keyCode < KeyStateTableSize)
                return keyStates[keyCode];
            else
                return KeyState::Released;
        }
        void QueueTask(const CoreLib::Event<> &f, void *owner)
        {
            uiThreadTaskQueueMutex.Lock();
            UIThreadTask task;
            task.ownerId = owner;
            task.callback = new CoreLib::Event<>(f);
            uiThreadTaskQueue.Add(CoreLib::_Move(task));
            uiThreadTaskQueueMutex.Unlock();
        }
    };
}

#endif

#endif