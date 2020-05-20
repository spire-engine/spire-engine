#ifdef __linux__

#ifndef GAME_ENGINE_MESSAGE_BOX_LINUX_H
#define GAME_ENGINE_MESSAGE_BOX_LINUX_H

#include "SystemWindow-Linux.h"
#include "UISystemBase.h"

namespace GameEngine
{
    class MessageBoxWindow
    {
    private:
        CoreLib::RefPtr<LinuxSystemWindow> internalWindow = nullptr;
        CoreLib::Array<GraphicsUI::Button *, 3> buttons;
        MessageBoxFlags flags;
        GraphicsUI::Container *bottomPanel;
        GraphicsUI::MultiLineTextBox *textBox;
        void OnButtonClick(GraphicsUI::UI_Base *sender);

    public:
        MessageBoxWindow();
        void Config(CoreLib::String text, CoreLib::String title, MessageBoxFlags msgBoxFlags);
        DialogResult Show(SystemWindow *parentWindow);
    };
} // namespace GameEngine

#endif

#endif