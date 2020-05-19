#ifdef __linux__
#include "FileDialog-Linux.h"
#include "CoreLib/LibUI/LibUI.h"
#include "CoreLib/LibUI/KeyCode.h"
#include "Engine.h"

#if __has_include(<filesystem>)
#define CPP17_FILESYSTEM 1
#include <filesystem>
#else
#warning "C++17 filesystem not provided by the compiler. FileDialog will not function correctly."
#endif

namespace GameEngine
{
    FileDialogWindow::FileDialogWindow(CoreLib::String defaultPath, CoreLib::String pattern, FileDialogType dialogType)
    {
        type = dialogType;
        internalWindow = dynamic_cast<LinuxSystemWindow*>(Engine::Instance()->CreateSystemWindow());
        if (type == FileDialogType::Open)
            internalWindow->SetText("Open File");
        else
            internalWindow->SetText("Save File");
        internalWindow->SetClientWidth(EM(30.0f));
        internalWindow->SetClientHeight(EM(22.0f));
        auto uiEntry = internalWindow->GetUIEntry();
        uiEntry->Padding = EM(0.5f);
        auto topPanel = new GraphicsUI::Container(uiEntry);
        topPanel->SetHeight(EM(1.5f));
        topPanel->Padding.Bottom = EM(0.2f);
        topPanel->DockStyle = GraphicsUI::Control::dsTop;
        auto topRightPanel = new GraphicsUI::Container(topPanel);
        topRightPanel->SetWidth(EM(1.3f));
        topRightPanel->DockStyle = GraphicsUI::Control::dsLeft;
        btnBack = new GraphicsUI::Button(topRightPanel);
        btnBack->SetFont(Engine::Instance()->GetUISystemInterface()->LoadDefaultFont(internalWindow->GetUIContext(), GraphicsUI::DefaultFontType::Symbol));
        btnBack->SetText("3");
        btnBack->Posit(0, 0, EM(1.2f), EM(1.0f));
        txtPath = new GraphicsUI::TextBox(topPanel);
        txtPath->DockStyle = GraphicsUI::Control::dsFill;
        txtPath->SetText(defaultPath);
        btnBack->SetHeight(txtPath->GetHeight());
        btnBack->OnClick.Bind(this, &FileDialogWindow::btnBack_Click);
        auto bottomPanel = new GraphicsUI::Container(uiEntry);
        auto buttonPanel = new GraphicsUI::Container(bottomPanel);
        buttonPanel->DockStyle = GraphicsUI::Control::dsBottom;
        buttonPanel->SetHeight(EM(2.0f));
        bottomPanel->SetHeight(EM(4.0f));
        bottomPanel->Padding.Top = EM(0.5f);
        buttonPanel->Padding.Top = EM(0.5f);
        bottomPanel->DockStyle = GraphicsUI::Control::dsBottom;
        auto label = new GraphicsUI::Label(bottomPanel);
        label->SetText("File name: ");
        label->AutoWidth = true;
        label->DockStyle = GraphicsUI::Control::dsLeft;
        txtFileName = new GraphicsUI::TextBox(bottomPanel);
        txtFileName->DockStyle = GraphicsUI::Control::dsFill;
        auto buttonRightPanel = new GraphicsUI::Container(buttonPanel);
        buttonRightPanel->DockStyle = GraphicsUI::Control::dsRight;
        
        btnOK = new GraphicsUI::Button(buttonRightPanel);
        btnCancel = new GraphicsUI::Button(buttonRightPanel);
        btnOK->SetText("OK");
        btnCancel->SetText("Cancel");
        btnCancel->OnClick.Bind([this](auto) { internalWindow->SetDialogResult(DialogResult::Cancel); });

        btnOK->Posit(0, 0, EM(4.0f), EM(1.5f));
        btnOK->OnClick.Bind(this, &FileDialogWindow::btnOK_Click);
        btnCancel->Posit(EM(4.5f), 0, EM(4.0f), EM(1.5));
        buttonRightPanel->SetWidth(EM(8.5f));

        lstFiles = new GraphicsUI::ListBox(uiEntry);
        lstFiles->DockStyle = GraphicsUI::Control::dsFill;
        lstFiles->OnMouseUp.Bind(this, &FileDialogWindow::lstFiles_MouseUp);
        lstFiles->OnDblClick.Bind(this, &FileDialogWindow::lstFiles_DblClick);

        txtFileName->OnKeyPress.Bind(this, &FileDialogWindow::txtFileName_KeyPress);
        txtPath->OnKeyPress.Bind(this, &FileDialogWindow::txtPath_KeyPress);

        ChangeDirectory(defaultPath);
    }

    void FileDialogWindow::TryCommitSelection(CoreLib::String selection)
    {
        if (type == FileDialogType::Open)
        {
            if (CoreLib::IO::Path::IsDirectory(selection) || selection.EndsWith(CoreLib::IO::Path::PathDelimiter))
            {
                ChangeDirectory(selection);
            }
            else if (CoreLib::IO::File::Exists(selection))
            {
                selectedFileName = selection;
                internalWindow->SetDialogResult(DialogResult::OK);
            }
            else
            {
                internalWindow->ShowMessage("File '" + selection + "' does not exist.", "Error", MessageBoxFlags::OKOnly);
            }
        }
        else
        {
            if (CoreLib::IO::File::Exists(selection))
            {
                if (CoreLib::IO::Path::IsDirectory(selection) || selection.EndsWith(CoreLib::IO::Path::PathDelimiter))
                {
                    ChangeDirectory(selection);
                }
                else if (internalWindow->ShowMessage("File '" + selection + "' already exists, overwrite?", "Warning", MessageBoxFlags::OKCancel)
                    != DialogResult::OK)
                {
                    return;
                }
            }
            if (CoreLib::IO::Path::GetFileExt(selection).Length() == 0 && defaultExt.Length() != 0)
                selection = selection + "." + defaultExt;
            selectedFileName = selection;
            internalWindow->SetDialogResult(DialogResult::OK);
        }
    }

    void FileDialogWindow::lstFiles_MouseUp(GraphicsUI::UI_Base * /*sender*/, GraphicsUI::UIMouseEventArgs& /*args*/)
    {
        if (lstFiles->SelectedIndex != -1)
        {
            auto selectedItem = lstFiles->GetTextItem(lstFiles->SelectedIndex)->GetText();
            txtFileName->SetText(selectedItem);
        }
    }
    void FileDialogWindow::lstFiles_DblClick(GraphicsUI::UI_Base * /*sender*/)
    {
        if (lstFiles->SelectedIndex != -1)
        {
            auto selectedItem = lstFiles->GetTextItem(lstFiles->SelectedIndex)->GetText();
            TryCommitSelection(CoreLib::IO::Path::Combine(currentDir, selectedItem));
        }
    }

    void FileDialogWindow::txtFileName_KeyPress(GraphicsUI::UI_Base *sender, GraphicsUI::UIKeyEventArgs &args)
    {
        if (args.Key == CoreLib::Keys::Return)
        {
            btnOK_Click(sender);
        }
    }

    void FileDialogWindow::txtPath_KeyPress(GraphicsUI::UI_Base *sender, GraphicsUI::UIKeyEventArgs &args)
    {
        if (args.Key == CoreLib::Keys::Return)
        {
            if (CoreLib::IO::Path::IsDirectory(txtPath->GetText()))
            {
                ChangeDirectory(txtPath->GetText());
            }
        }
    }

    void FileDialogWindow::ChangeDirectory(CoreLib::String dir)
    {
        if (CoreLib::IO::Path::IsDirectory(dir))
        {
            if ((dir.EndsWith(CoreLib::IO::Path::PathDelimiter) || dir.EndsWith(CoreLib::IO::Path::AltPathDelimiter)) && dir.Length() > 1)
            {
                currentDir = dir.SubString(0, dir.Length() - 1);
            }
            else
            {
                currentDir = dir;
            }
            txtPath->SetText(currentDir);
            lstFiles->Clear();
#ifdef CPP17_FILESYSTEM
            for (auto f : std::filesystem::directory_iterator(dir.Buffer()))
            {
                if (f.is_directory())
                {
                    lstFiles->AddTextItem(CoreLib::IO::Path::GetFileName(f.path().c_str()) + CoreLib::IO::Path::PathDelimiter);
                }
                else
                {
                    lstFiles->AddTextItem(CoreLib::IO::Path::GetFileName(f.path().c_str()));
                }
            }
#endif
        }
    }

    void FileDialogWindow::btnOK_Click(GraphicsUI::UI_Base* sender)
    {
#ifdef CPP17_FILESYSTEM
        if (std::filesystem::path(txtFileName->GetText().Buffer()).is_absolute())
        {
            TryCommitSelection(txtFileName->GetText());
            return;
        }
        else
        {
            auto combinedPath = CoreLib::IO::Path::Combine(currentDir, txtFileName->GetText());
            TryCommitSelection(combinedPath);
            return;
        }
#endif
    }

    void FileDialogWindow::btnBack_Click(GraphicsUI::UI_Base* sender)
    {
        auto parentDir = CoreLib::IO::Path::GetDirectoryName(currentDir);
        ChangeDirectory(parentDir);
    }

    DialogResult FileDialogWindow::Show(SystemWindow* parentWindow)
    {
        return internalWindow->ShowModal(parentWindow);
    }

    CoreLib::String FileDialogWindow::GetFileName()
    {
        return selectedFileName;
    }
}

#endif