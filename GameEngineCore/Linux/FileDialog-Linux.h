#ifdef __linux__

#ifndef GAME_ENGINE_FILE_DIALOG_LINUX_H
#define GAME_ENGINE_FILE_DIALOG_LINUX_H

#include "SystemWindow-Linux.h"
#include "UISystemBase.h"

namespace GameEngine
{
    enum class FileDialogType
    {
        Open, Save
    };
    class FileDialogWindow
    {
    private:
        CoreLib::RefPtr<LinuxSystemWindow> internalWindow = nullptr;

        GraphicsUI::ListBox *lstFiles;
        GraphicsUI::TextBox *txtPath;
        GraphicsUI::Button *btnOK, *btnCancel, *btnBack;
        GraphicsUI::TextBox *txtFileName;
        CoreLib::HashSet<CoreLib::String> filters;
        CoreLib::String currentDir, selectedFileName;
        FileDialogType type;

        void btnOK_Click(GraphicsUI::UI_Base* sender);
        void btnBack_Click(GraphicsUI::UI_Base* sender);

        void lstFiles_MouseUp(GraphicsUI::UI_Base *sender, GraphicsUI::UIMouseEventArgs& args);
        void lstFiles_DblClick(GraphicsUI::UI_Base *sender);

        void ChangeDirectory(CoreLib::String dir);
        void TryCommitSelection(CoreLib::String selection);

        void txtFileName_KeyPress(GraphicsUI::UI_Base *sender, GraphicsUI::UIKeyEventArgs &args);
        void txtPath_KeyPress(GraphicsUI::UI_Base *sender, GraphicsUI::UIKeyEventArgs &args);

    public:
        FileDialogWindow(CoreLib::String defaultPath, CoreLib::String pattern, FileDialogType dialogType);
        CoreLib::String defaultExt;
        DialogResult Show(SystemWindow *parentWindow);
        CoreLib::String GetFileName();
    };
} // namespace GameEngine

#endif

#endif