#ifdef __linux__
#include "MessageBoxWindow-Linux.h"
#include "CoreLib/LibUI/LibUI.h"
#include "Engine.h"

namespace GameEngine
{
    MessageBoxWindow::MessageBoxWindow(CoreLib::String text, CoreLib::String title, MessageBoxFlags msgBoxFlags)
    {
        flags = msgBoxFlags;
        internalWindow = dynamic_cast<LinuxSystemWindow*>(Engine::Instance()->CreateSystemWindow());
        internalWindow->SetText(title);
        auto uiEntry = internalWindow->GetUIEntry();
        uiEntry->Padding = EM(1.0f);
        auto bottomPanel = new GraphicsUI::Container(uiEntry);
        bottomPanel->SetHeight(EM(2.0f));
        bottomPanel->DockStyle = GraphicsUI::Control::dsBottom;
        auto textBox = GraphicsUI::CreateMultiLineTextBox(uiEntry);
        textBox->SetText(text);
        textBox->Enabled = false;
        auto textSize = textBox->GetFont()->MeasureString(text, GraphicsUI::DrawTextOptions());
        const int maxWidth = EM(30.0f);
        int width = CoreLib::Math::Min(maxWidth, textSize.w + EM(4.0f));
        width = CoreLib::Math::Max(width, EM(20.0f));
        internalWindow->SetClientWidth(width);
        internalWindow->SetClientHeight(EM(8.0f));
        internalWindow->SetFixedSize();
        textBox->SetScrollBars(false, false);
        textBox->SetReadOnly(true);
        textBox->SetBackgroundColor(uiEntry->BackColor);
        textBox->TabStop = false;
        textBox->BorderStyle = GraphicsUI::BS_NONE;
        textBox->DockStyle = GraphicsUI::Control::dsFill;
        switch (flags)
        {
        case MessageBoxFlags::OKOnly:
            {
                auto btnOK = new GraphicsUI::Button(bottomPanel, "OK");
                btnOK->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnOK);
                break;
            }
        case MessageBoxFlags::OKCancel:
            {
                auto btnOK = new GraphicsUI::Button(bottomPanel, "OK");
                btnOK->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnOK);
                auto btnCancel = new GraphicsUI::Button(bottomPanel, "Cancel");
                btnCancel->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnCancel);
                break;
            }
        case MessageBoxFlags::YesNo:
            {
                auto btnYes = new GraphicsUI::Button(bottomPanel, "Yes");
                btnYes->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnYes);
                auto btnNo = new GraphicsUI::Button(bottomPanel, "No");
                btnNo->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnNo);
                break;
            }
        case MessageBoxFlags::YesNoCancel:
            {
                auto btnYes = new GraphicsUI::Button(bottomPanel, "Yes");
                btnYes->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnYes);
                auto btnNo = new GraphicsUI::Button(bottomPanel, "No");
                btnNo->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnNo);
                auto btnCancel = new GraphicsUI::Button(bottomPanel, "Cancel");
                btnCancel->OnClick.Bind(this, &MessageBoxWindow::OnButtonClick);
                buttons.Add(btnCancel);
                break;
            }
        }
        const int buttonWidth = EM(4.5f);
        const int clientWidth = width - EM(2.0f);
        const int totalButtonWidth = buttonWidth * buttons.Count() + EM(0.5f) * (buttons.Count() - 1);
        const int buttonLeft = (clientWidth - totalButtonWidth) / 2;
        for (int i = 0; i < buttons.Count(); i++)
        {
            buttons[i]->Posit(buttonLeft + (buttonWidth + EM(0.5f)) * i, EM(0.5f), buttonWidth, EM(1.5f));
        }
        buttons[0]->SetFocus();
    }

    void MessageBoxWindow::OnButtonClick(GraphicsUI::UI_Base* sender)
    {
        DialogResult result = DialogResult::Undefined;
        if (sender == buttons[0])
        {
            switch (flags)
            {
            case MessageBoxFlags::OKCancel:
            case MessageBoxFlags::OKOnly:
                result = DialogResult::OK;
                break;
            default:
                result = DialogResult::Yes;
                break;
            }
        }
        else if (sender == buttons[1])
        {
            switch (flags)
            {
            case MessageBoxFlags::OKCancel:
                result = DialogResult::Cancel;
                break;
            default:
                result = DialogResult::No;
                break;
            }
        }
        else
        {
            result = DialogResult::Cancel;
        }
        internalWindow->SetDialogResult(result);
    }

    DialogResult MessageBoxWindow::Show(SystemWindow* parentWindow)
    {
        return internalWindow->ShowModal(parentWindow);
    }
}

#endif