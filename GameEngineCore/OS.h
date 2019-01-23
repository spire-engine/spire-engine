#ifndef GAME_ENGINE_OS_H
#define GAME_ENGINE_OS_H

#include "CoreLib/LibUI/UISystemInterface.h"
#include "CoreLib/LibUI/LibUI.h"

namespace GameEngine
{
	typedef void* WindowHandle;
	enum class RenderAPI
	{
		Vulkan
	};

    class HardwareRenderer;
    class Texture2D;

    enum class DialogResult
    {
        Undefined, Cancel, OK, Yes, No
    };

    enum class MessageBoxFlags : int
    {
        OKOnly,
        OKCancel,
        YesNo,
        YesNoCancel
    };

    class SystemWindow : public CoreLib::RefObject
    {
    public:
        CoreLib::Event<> SizeChanged;
        virtual GraphicsUI::UIEntry * GetUIEntry() = 0;
        virtual GraphicsUI::UIWindowContext* GetUIContext() = 0;
        virtual void SetClientWidth(int w) = 0;
        virtual void SetClientHeight(int h) = 0;
        virtual int GetClientWidth() = 0;
        virtual int GetClientHeight() = 0;
        virtual void CenterScreen() = 0;
        virtual void Close() = 0;
        virtual bool Focused() = 0;
        virtual WindowHandle GetNativeHandle() = 0;
        virtual void SetText(CoreLib::String text) = 0;
        virtual bool IsVisible() = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags = MessageBoxFlags::OKOnly) = 0;
    };

    class FileDialog : public CoreLib::RefObject
    {
    private:
        SystemWindow * owner;
        CoreLib::String initDir;
    public:
        CoreLib::String Filter;
        CoreLib::String DefaultEXT;
        CoreLib::String FileName;
        CoreLib::List<CoreLib::String> FileNames;
        bool MultiSelect = false;
        bool FileMustExist = false;
        bool HideReadOnly = false;
        bool CreatePrompt = false;
        bool OverwritePrompt = false;
        bool PathMustExist = false;
        virtual bool ShowOpen() = 0;
        virtual bool ShowSave() = 0;
    };

    class Font
    {
    public:
        CoreLib::String FontName;
        int Size = 9;
        bool Bold = false, Underline = false, Italic = false, StrikeOut = false;
        Font();
        Font(const CoreLib::String& sname, int ssize)
        {
            FontName = sname;
            Size = ssize;
            Bold = false;
            Underline = false;
            Italic = false;
            StrikeOut = false;
        }
        Font(const CoreLib::String & sname, int ssize, bool sBold, bool sItalic, bool sUnderline)
        {
            FontName = sname;
            Size = ssize;
            Bold = sBold;
            Underline = sUnderline;
            Italic = sItalic;
            StrikeOut = false;
        }
        CoreLib::String ToString() const
        {
            CoreLib::StringBuilder sb;
            sb << FontName << Size << Bold << Underline << Italic << StrikeOut;
            return sb.ProduceString();
        }
    };

    class OsTimer : public CoreLib::RefObject
    {
    public:
        CoreLib::Event<> Tick;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void SetInterval(int val) = 0;
    };

    class OsApplication
    {
    public:
        static GraphicsUI::ISystemInterface* CreateUISystemInterface(HardwareRenderer * renderer);
        static SystemWindow* CreateSystemWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize);
        static OsTimer* CreateTimer();
        static FileDialog* CreateFileDialog(SystemWindow* parent);
        static void SetMainLoopEventHandler(CoreLib::Procedure<> handler);
        static DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags = MessageBoxFlags::OKOnly);
        static void Run(SystemWindow* mainWindow);
        static void Dispose();
        static void DebugPrint(const char * buffer);
        static void DebugWriteLine(const char * buffer)
        {
            DebugPrint(buffer);
            DebugPrint("\n");
        }
    };
}

#endif