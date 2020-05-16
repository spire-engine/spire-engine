#ifndef GAME_ENGINE_OS_H
#define GAME_ENGINE_OS_H

#include "CoreLib/LibUI/UISystemInterface.h"
#include "CoreLib/LibUI/LibUI.h"
#include "CoreLib/CommandLineParser.h"

namespace GameEngine
{
#if defined(_WIN32)
	typedef uint64_t WindowHandle;
    inline CoreLib::String WindowHandleToString(WindowHandle handle)
    {
        return CoreLib::String((long long)handle);
    }
#elif defined(__linux__)
    struct WindowHandle
    {
        void* display = nullptr;
        uint32_t window = 0;
        operator bool()
        {
            return window;
        }
    };
    inline CoreLib::String WindowHandleToString(WindowHandle handle)
    {
        return CoreLib::String(handle.window);
    }
#endif
	enum class RenderAPI
	{
		Vulkan, Dummy
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
        virtual void Invoke(const CoreLib::Event<> & func) = 0;
        virtual void InvokeAsync(const CoreLib::Event<> & func) = 0;
        virtual int GetCurrentDpi() = 0;
        virtual DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags = MessageBoxFlags::OKOnly) = 0;
    };

    class OsFileDialog : public CoreLib::RefObject
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

    // ==============================================
    // Interface for text rasterization.
 
    struct TextSize
    {
        int x, y;
    };

    class TextRasterizationResult
    {
    public:
        TextSize Size;
        unsigned char* ImageData;
    };

    class OsFontRasterizer : public CoreLib::RefObject
    {
    public:
        virtual void SetFont(const Font& Font, int dpi) = 0;
        virtual TextRasterizationResult RasterizeText(const CoreLib::String& text, const GraphicsUI::DrawTextOptions& options) = 0;
        virtual TextSize GetTextSize(const CoreLib::String& text, const GraphicsUI::DrawTextOptions& options) = 0;
        virtual TextSize GetTextSize(const CoreLib::List<unsigned int>& text, const GraphicsUI::DrawTextOptions& options) = 0;
    };

    // ===============================================
    // System timer.

    class OsTimer : public CoreLib::RefObject
    {
    public:
        CoreLib::Event<> Tick;
        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual void SetInterval(int val) = 0;
    };

    // ===============================================
    // Main context that serves as entry point to all OS APIs.

    class OsApplication
    {
    private:
        static CoreLib::Text::CommandLineParser commandlineParser;
    public:
        static GraphicsUI::ISystemInterface* CreateUISystemInterface(HardwareRenderer * renderer);
        static SystemWindow* CreateSystemWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize);
        static SystemWindow* CreateDummyWindow(GraphicsUI::ISystemInterface* sysInterface, int log2BufferSize);
        static OsTimer* CreateTimer();
        static OsFontRasterizer* CreateFontRasterizer();
        static OsFileDialog* CreateFileDialog(SystemWindow* parent);
        static void SetMainLoopEventHandler(CoreLib::Procedure<> handler);
        static DialogResult ShowMessage(CoreLib::String msg, CoreLib::String title, MessageBoxFlags flags = MessageBoxFlags::OKOnly);
        static void Run(SystemWindow* mainWindow);
        static void Quit();
        static void DoEvents();
        static void Init(int argc, const char** argv);
        static void Dispose();
        static void DebugPrint(const char * buffer);
        static CoreLib::Text::CommandLineParser& GetCommandLineParser()
        {
            return commandlineParser;
        }
        static void DebugWriteLine(const char * buffer)
        {
            DebugPrint(buffer);
            DebugPrint("\n");
        }
    };
}

#endif