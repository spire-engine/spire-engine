#ifndef GAME_ENGINE_UI_SYSTEM_BASE_H
#define GAME_ENGINE_UI_SYSTEM_BASE_H

#include "CoreLib/Basic.h"
#include "CoreLib/LibUI/LibUI.h"
#include "CoreLib/Imaging/Bitmap.h"
#include "CoreLib/MemoryPool.h"
#include "HardwareRenderer.h"
#include "AsyncCommandBuffer.h"
#include "EngineLimits.h"
#include "CoreLib/LibMath.h"
#include "OS.h"

namespace GameEngine
{
    class GLUIRenderer;
    class UISystemBase;

    const int TextBufferSize = 6 * 1024 * 1024;
    const int TextPixelBits = 4;
    const int Log2TextPixelsPerByte = CoreLib::Math::Log2Floor(8 / TextPixelBits);
    const int Log2TextBufferBlockSize = 6;

    class SystemFont : public GraphicsUI::IFont
    {
    public:
        CoreLib::RefPtr<OsFontRasterizer> rasterizer;
        UISystemBase* system;
        SystemWindow* window = nullptr;
        Font fontDesc;
    public:
        SystemFont(UISystemBase* ctx, SystemWindow* associatedWindow, const Font& font)
        {
            system = ctx;
            window = associatedWindow;
            fontDesc = font;
            rasterizer = OsApplication::CreateFontRasterizer();
            UpdateFontContext(associatedWindow->GetCurrentDpi());
        }
        void UpdateFontContext(int dpi)
        {
            rasterizer->SetFont(fontDesc, dpi);
        }
        SystemWindow* GetAssociatedWindow()
        {
            return window;
        }
        virtual GraphicsUI::Rect MeasureString(const CoreLib::String& text, GraphicsUI::DrawTextOptions options) override;
        virtual GraphicsUI::Rect MeasureString(const CoreLib::List<unsigned int>& text, GraphicsUI::DrawTextOptions options) override;
        virtual GraphicsUI::IBakedText* BakeString(const CoreLib::String& text, GraphicsUI::IBakedText* previous, GraphicsUI::DrawTextOptions options) override;
    };

    class BakedText : public GraphicsUI::IBakedText
    {
    public:
        UISystemBase* system = nullptr;
        unsigned char * textBuffer = nullptr;
        SystemFont* font = nullptr;
        CoreLib::String textContent;
        int64_t lastUse = 0;
        int BufferSize = 0;
        GraphicsUI::DrawTextOptions options;
        int Width = 0, Height = 0;
    public:
        void Rebake();
        virtual int GetWidth() override
        {
            return Width;
        }
        virtual int GetHeight() override
        {
            return Height;
        }
        ~BakedText();
    };

    class UIWindowContext : public GraphicsUI::UIWindowContext
    {
    private:
        void TickTimerTick();
        void HoverTimerTick();
    public:
        int screenWidth, screenHeight;
        int primitiveBufferSize, vertexBufferSize, indexBufferSize;
        SystemWindow * window;
        UISystemBase* sysInterface;
        HardwareRenderer * hwRenderer;
        CoreLib::RefPtr<WindowSurface> surface;
        CoreLib::RefPtr<Buffer> vertexBuffer, indexBuffer, primitiveBuffer;
        CoreLib::RefPtr<GraphicsUI::UIEntry> uiEntry;
        CoreLib::RefPtr<Texture2D> uiOverlayTexture;
        CoreLib::RefPtr<FrameBuffer> frameBuffer;
        CoreLib::RefPtr<Buffer> uniformBuffer;
        CoreLib::RefPtr<OsTimer> tmrHover, tmrTick;
        CoreLib::Array<CoreLib::RefPtr<DescriptorSet>, DynamicBufferLengthMultiplier> descSets;
        VectorMath::Matrix4 orthoMatrix;
        CoreLib::RefPtr<AsyncCommandBuffer> cmdBuffer, blitCmdBuffer;
        void SetSize(int w, int h);
        UIWindowContext();
        ~UIWindowContext();
    };

    class UISystemBase : public GraphicsUI::ISystemInterface
    {
    protected:
        unsigned char * textBuffer = nullptr;
        CoreLib::EnumerableDictionary<CoreLib::String, CoreLib::RefPtr<SystemFont>> fonts;
        CoreLib::RefPtr<Buffer> textBufferObj;
        CoreLib::MemoryPool textBufferPool;
        VectorMath::Vec4 ColorToVec(GraphicsUI::Color c);
        Fence* textBufferFence = nullptr;
    public:
        CoreLib::EnumerableHashSet<BakedText*> bakedTexts;
        GLUIRenderer * uiRenderer;
        CoreLib::EnumerableDictionary<SystemWindow*, UIWindowContext*> windowContexts;
        HardwareRenderer * rendererApi = nullptr;
    public:
        UISystemBase(HardwareRenderer * ctx);
        ~UISystemBase();
        void WaitForDrawFence();
        unsigned char * AllocTextBuffer(int size);
        void FreeTextBuffer(unsigned char * buffer, int size)
        {
            textBufferPool.Free(buffer, size);
        }
        int GetTextBufferRelativeAddress(unsigned char * buffer)
        {
            return (int)(buffer - textBuffer);
        }
        Buffer * GetTextBufferObject()
        {
            return textBufferObj.Ptr();
        }
        GraphicsUI::IImage * CreateImageObject(const CoreLib::Imaging::Bitmap & bmp);
        void TransferDrawCommands(UIWindowContext * ctx, Texture2D* baseTexture, WindowBounds viewport, CoreLib::List<GraphicsUI::DrawCommand> & commands);
        void QueueDrawCommands(UIWindowContext * ctx, Fence* fence);
        FrameBuffer * CreateFrameBuffer(Texture2D * texture);
        CoreLib::RefPtr<UIWindowContext> CreateWindowContext(SystemWindow* handle, int w, int h, int log2BufferSize);
        void UnregisterWindowContext(UIWindowContext * ctx);
        GraphicsUI::IFont * LoadFont(UIWindowContext * ctx, const Font & f);
    };
}

#endif