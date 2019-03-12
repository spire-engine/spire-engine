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

    const int TextBufferSize = 4 * 1024 * 1024;
    const int TextPixelBits = 4;
    const int Log2TextPixelsPerByte = CoreLib::Math::Log2Floor(8 / TextPixelBits);
    const int Log2TextBufferBlockSize = 6;

    class BakedText : public GraphicsUI::IBakedText
    {
    public:
        UISystemBase* system;
        unsigned char * textBuffer;
        int BufferSize;
        int Width, Height;
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
        CoreLib::Dictionary<CoreLib::String, CoreLib::RefPtr<GraphicsUI::IFont>> fonts;
        CoreLib::RefPtr<Buffer> textBufferObj;
        CoreLib::MemoryPool textBufferPool;
        VectorMath::Vec4 ColorToVec(GraphicsUI::Color c);
        Fence* textBufferFence = nullptr;
    public:
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
        virtual GraphicsUI::IFont * LoadFont(UIWindowContext * ctx, const Font & f) = 0;
    };
}

#endif