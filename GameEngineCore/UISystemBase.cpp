#include "UISystemBase.h"
#include "ShaderCompiler.h"

using namespace VectorMath;
using namespace CoreLib;

namespace GameEngine
{
    struct UberVertex
    {
        float x, y, u, v;
        int inputIndex;
    };

    struct TextUniformFields
    {
        int TextWidth, TextHeight;
        int StartPointer;
        int NotUsed;
    };

    struct ShadowUniformFields
    {
        unsigned short ShadowOriginX, ShadowOriginY, ShadowWidth, ShadowHeight;
        int ShadowSize;
        int NotUsed;
    };

    struct UniformField
    {
        unsigned short ClipRectX, ClipRectY, ClipRectX1, ClipRectY1;
        int ShaderType;
        GraphicsUI::Color InputColor;
        union
        {
            TextUniformFields TextParams;
            ShadowUniformFields ShadowParams;
        };
    };

    GraphicsUI::Rect SystemFont::MeasureString(const CoreLib::String& text, GraphicsUI::DrawTextOptions options)
    {
        GraphicsUI::Rect rs;
        auto size = rasterizer->GetTextSize(text, options);
        rs.x = rs.y = 0;
        rs.w = size.x;
        rs.h = size.y;
        return rs;
    }

    GraphicsUI::Rect SystemFont::MeasureString(const List<unsigned int>& text, GraphicsUI::DrawTextOptions options)
    {
        GraphicsUI::Rect rs;
        auto size = rasterizer->GetTextSize(text, options);
        rs.x = rs.y = 0;
        rs.w = size.x;
        rs.h = size.y;
        return rs;
    }

    GraphicsUI::IBakedText* SystemFont::BakeString(const CoreLib::String& text, GraphicsUI::IBakedText* previous, GraphicsUI::DrawTextOptions options)
    {
        BakedText* prev = (BakedText*)previous;
        auto prevBuffer = (prev ? prev->textBuffer : nullptr);
        system->WaitForDrawFence();
        BakedText* result = prev;
        if (!prevBuffer)
            result = new BakedText();
        result->font = this;
        result->options = options;
        result->textContent = text;
        result->system = system;
        result->Rebake();
        system->bakedTexts.Add(result);
        return result;
    }

    void BakedText::Rebake()
    {
        auto imageData = font->rasterizer->RasterizeText(textContent, this->options);
        Width = imageData.Size.x;
        Height = imageData.Size.y;
        // Allocate GPU text buffer.
        int pixelCount = (Width * Height);
        int bytes = pixelCount >> Log2TextPixelsPerByte;
        int textPixelMask = (1 << TextPixelBits) - 1;
        if (pixelCount & ((1 << Log2TextPixelsPerByte) - 1))
            bytes++;
        bytes = Math::RoundUpToAlignment(bytes, 1 << Log2TextBufferBlockSize);
        if (bytes > BufferSize)
        {
            if (textBuffer)
                system->FreeTextBuffer(textBuffer, BufferSize);
            textBuffer = system->AllocTextBuffer(bytes);
            BufferSize = bytes;
        }
        // Store compressed text data in the allocated GPU buffer.
        const float valScale = ((1 << TextPixelBits) - 1) / 255.0f;
        for (int i = 0; i < Height; i++)
        {
            for (int j = 0; j < Width; j++)
            {
                int idx = i * Width + j;
                auto val = imageData.ImageData[idx];
                auto packedVal = Math::FastFloor(val * valScale + 0.5f);
                int addr = idx >> Log2TextPixelsPerByte;
                int mod = idx & ((1 << Log2TextPixelsPerByte) - 1);
                int mask = textPixelMask << (mod * TextPixelBits);
                textBuffer[addr] = (unsigned char)((textBuffer[addr] & (~mask)) | (packedVal << (mod * TextPixelBits)));
            }
        }
    }

    BakedText::~BakedText()
    {
        system->bakedTexts.Remove(this);
        if (textBuffer)
            system->FreeTextBuffer(textBuffer, BufferSize);
    }


    void UIWindowContext::SetSize(int w, int h)
    {
        if (w == 0 || h == 0)
            return;
        hwRenderer->Wait();
        surface->Resize(w, h);
        uiEntry->Posit(0, 0, w, h);
        uiOverlayTexture = hwRenderer->CreateTexture2D("uiOverlayTexture",
            (TextureUsage)((int)TextureUsage::SampledColorAttachment | (int)TextureUsage::Storage), w, h, 1,
            StorageFormat::RGBA_8);
        frameBuffer = sysInterface->CreateFrameBuffer(uiOverlayTexture.Ptr());
        screenWidth = w;
        screenHeight = h;
        Matrix4::CreateIdentityMatrix(orthoMatrix);
        orthoMatrix.m[0][0] = 2.0f / screenWidth;
        orthoMatrix.m[1][1] = 2.0f / screenHeight;
        orthoMatrix.m[3][0] = orthoMatrix.m[3][1] = -1.0f;
        uniformBuffer->SetData(&orthoMatrix, sizeof(orthoMatrix));
    }

    GraphicsUI::IFont* UISystemBase::LoadFont(UIWindowContext* ctx, const Font& f)
    {
        auto identifier = f.ToString() + "_" + WindowHandleToString(ctx->window->GetNativeHandle());
        RefPtr<SystemFont> font;
        if (!fonts.TryGetValue(identifier, font))
        {
            font = new SystemFont(this, ctx->window, f);
            fonts[identifier] = font;
        }
        return font.Ptr();
    }

    UIWindowContext::UIWindowContext()
    {
        tmrHover = OsApplication::CreateTimer();
        tmrTick = OsApplication::CreateTimer();
        tmrHover->SetInterval(GraphicsUI::Global::HoverTimeThreshold);
        tmrHover->Tick.Bind(this, &UIWindowContext::HoverTimerTick);
        tmrTick->SetInterval(50);
        tmrTick->Tick.Bind(this, &UIWindowContext::TickTimerTick);
        tmrTick->Start();
    }

    UIWindowContext::~UIWindowContext()
    {
        tmrTick->Stop();
        sysInterface->UnregisterWindowContext(this);
        hwRenderer->Wait();
    }

    class GLUIRenderer
    {
    private:
        GameEngine::HardwareRenderer * rendererApi;
    private:
        RefPtr<Shader> uberVs, uberFs;
        RefPtr<Pipeline> pipeline;
        RefPtr<TextureSampler> linearSampler;
        RefPtr<RenderTargetLayout> renderTargetLayout;
        RefPtr<DescriptorSetLayout> descLayout;
        List<UniformField> uniformFields;
        List<UberVertex> vertexStream;
        List<int> indexStream;
        int primCounter;
        UISystemBase * system;
        Vec4 clipRect;
        int frameId = 0;
        int maxVertices = 0, maxIndices = 0, maxPrimitives = 0;
        int bufferReservior = 128;
    public:
        void SetBufferLimit(int maxVerts, int maxIndex, int maxPrim)
        {
            maxVertices = maxVerts;
            maxIndices = maxIndex;
            maxPrimitives = maxPrim;
        }
    public:
        GLUIRenderer(UISystemBase * pSystem, GameEngine::HardwareRenderer * hw)
        {
            system = pSystem;
            clipRect = Vec4::Create(0.0f, 0.0f, 1e20f, 1e20f);
            rendererApi = hw;
            ShaderCompilationResult crs;
            auto shaderSet = CompileGraphicsShader(crs, hw, "UI.slang");

            if (!shaderSet.fragmentShader)
            {
                throw InvalidProgramException("UI shader compilation failed.");
            }

            Array<Shader*, 2> shaderList;
            shaderList.Add(shaderSet.vertexShader.Ptr()); shaderList.Add(shaderSet.fragmentShader.Ptr());
            RefPtr<PipelineBuilder> pipeBuilder = rendererApi->CreatePipelineBuilder();
            pipeBuilder->SetShaders(shaderList.GetArrayView());
            VertexFormat vformat;
            vformat.Attributes.Add(VertexAttributeDesc(DataType::Float2, 0, 0, 0, "POSITION", 0));
            vformat.Attributes.Add(VertexAttributeDesc(DataType::Float2, 0, 8, 1, "TEXCOORD", 0));
            vformat.Attributes.Add(VertexAttributeDesc(DataType::Int, 0, 16, 2, "PRIMID", 0));
            pipeBuilder->SetVertexLayout(vformat);
            descLayout = rendererApi->CreateDescriptorSetLayout(MakeArray(
                DescriptorLayout(sfGraphics, 0, BindingType::UniformBuffer),
                DescriptorLayout(sfGraphics, 1, BindingType::StorageBuffer),
                DescriptorLayout(sfGraphics, 2, BindingType::StorageBuffer)).GetArrayView());
            pipeBuilder->SetBindingLayout(MakeArrayView(descLayout.Ptr()));
            pipeBuilder->FixedFunctionStates.PrimitiveRestartEnabled = true;
            pipeBuilder->FixedFunctionStates.PrimitiveTopology = PrimitiveType::TriangleStrips;
            pipeBuilder->FixedFunctionStates.blendMode = BlendMode::AlphaBlend;
            pipeBuilder->FixedFunctionStates.DepthCompareFunc = CompareFunc::Disabled;
            pipeBuilder->FixedFunctionStates.cullMode = CullMode::Disabled;


            Array<AttachmentLayout, 1> frameBufferLayout;
            frameBufferLayout.Add(AttachmentLayout(TextureUsage::SampledColorAttachment, StorageFormat::RGBA_8));

            renderTargetLayout = rendererApi->CreateRenderTargetLayout(frameBufferLayout.GetArrayView(), true);
            pipeBuilder->SetDebugName("ui");
            pipeline = pipeBuilder->ToPipeline(renderTargetLayout.Ptr());

            linearSampler = rendererApi->CreateTextureSampler();
            linearSampler->SetFilter(TextureFilter::Linear);
        }
        ~GLUIRenderer()
        {

        }
        DescriptorSetLayout * GetDescLayout()
        {
            return descLayout.Ptr();
        }
        FrameBuffer * CreateFrameBuffer(Texture2D * texture)
        {
            return renderTargetLayout->CreateFrameBuffer(MakeArrayView(texture));
        }
        void BeginUIDrawing()
        {
            vertexStream.Clear();
            uniformFields.Clear();
            indexStream.Clear();
            primCounter = 0;
        }
        void EndUIDrawing(UIWindowContext * wndCtx)
        {
            frameId = frameId % DynamicBufferLengthMultiplier;
            int indexCount = indexStream.Count();
            if (indexCount * (int)sizeof(int) > wndCtx->indexBufferSize)
                indexCount = wndCtx->indexBufferSize / (int)sizeof(int);
            wndCtx->indexBuffer->SetDataAsync(frameId * wndCtx->indexBufferSize, indexStream.Buffer(),
                Math::Min((int)sizeof(int) * indexStream.Count(), wndCtx->indexBufferSize));
            wndCtx->vertexBuffer->SetDataAsync(frameId * wndCtx->vertexBufferSize, vertexStream.Buffer(),
                Math::Min((int)sizeof(UberVertex) * vertexStream.Count(), wndCtx->vertexBufferSize));
            wndCtx->primitiveBuffer->SetDataAsync(frameId * wndCtx->primitiveBufferSize, uniformFields.Buffer(),
                Math::Min((int)sizeof(UniformField) * uniformFields.Count(), wndCtx->primitiveBufferSize));

            auto cmdBuf = wndCtx->cmdBuffer->BeginRecording(wndCtx->frameBuffer.Ptr());
            cmdBuf->BindPipeline(pipeline.Ptr());
            cmdBuf->BindVertexBuffer(wndCtx->vertexBuffer.Ptr(), frameId * wndCtx->vertexBufferSize);
            cmdBuf->BindIndexBuffer(wndCtx->indexBuffer.Ptr(), frameId * wndCtx->indexBufferSize);
            cmdBuf->BindDescriptorSet(0, wndCtx->descSets[frameId].Ptr());
            cmdBuf->SetViewport(Viewport(0, 0, wndCtx->screenWidth, wndCtx->screenHeight));
            cmdBuf->DrawIndexed(0, indexCount);
            cmdBuf->EndRecording();
        }
        void SubmitCommands(Texture2D* baseTexture, WindowBounds viewport, UIWindowContext * wndCtx)
        {
            frameId++;

            if (baseTexture)
            {
                rendererApi->Blit(wndCtx->uiOverlayTexture.Ptr(), baseTexture,
                    VectorMath::Vec2i::Create(viewport.x, viewport.y), SourceFlipMode::ForPresent);
            }

            rendererApi->QueueRenderPass(
                wndCtx->frameBuffer.Ptr(), (baseTexture == nullptr),
                MakeArrayView(wndCtx->cmdBuffer->GetBuffer()));
        }
        bool IsBufferFull()
        {
            return (indexStream.Count() + bufferReservior > maxIndices ||
                vertexStream.Count() + bufferReservior > maxVertices ||
                primCounter + bufferReservior > maxPrimitives);
        }
        void DrawLine(const GraphicsUI::Color & color, float width, float x0, float y0, float x1, float y1)
        {
            if (IsBufferFull())
                return;
            UberVertex points[4];
            Vec2 p0 = Vec2::Create(x0, y0);
            Vec2 p1 = Vec2::Create(x1, y1);
            Vec2 lineDir = (p1 - p0) * 0.5f;
            lineDir = lineDir.Normalize();
            lineDir = lineDir;
            Vec2 lineDirOtho = Vec2::Create(-lineDir.y, lineDir.x);
            float halfWidth = width * 0.5f;
            lineDirOtho *= halfWidth;
            p0.x -= lineDir.x; p0.y -= lineDir.y;
            //p1.x += lineDir.x; p1.y += lineDir.y;
            points[0].x = p0.x - lineDirOtho.x; points[0].y = p0.y - lineDirOtho.y; points[0].inputIndex = primCounter;
            points[1].x = p0.x + lineDirOtho.x; points[1].y = p0.y + lineDirOtho.y; points[1].inputIndex = primCounter;
            points[2].x = p1.x - lineDirOtho.x; points[2].y = p1.y - lineDirOtho.y; points[2].inputIndex = primCounter;
            points[3].x = p1.x + lineDirOtho.x; points[3].y = p1.y + lineDirOtho.y; points[3].inputIndex = primCounter;
            indexStream.Add(vertexStream.Count());
            indexStream.Add(vertexStream.Count() + 1);
            indexStream.Add(vertexStream.Count() + 2);
            indexStream.Add(vertexStream.Count() + 3);
            indexStream.Add(-1);
            vertexStream.AddRange(points, 4);
            UniformField fields;
            fields.ClipRectX = (unsigned short)clipRect.x;
            fields.ClipRectY = (unsigned short)clipRect.y;
            fields.ClipRectX1 = (unsigned short)clipRect.z;
            fields.ClipRectY1 = (unsigned short)clipRect.w;
            fields.ShaderType = 0;
            fields.InputColor = color;
            uniformFields.Add(fields);
            primCounter++;
        }
        void DrawLineCap(GraphicsUI::Color color, Vec2 pos, Vec2 dir, float size)
        {
            if (IsBufferFull())
                return;
            Array<Vec2, 3> points;
            points.SetSize(3);
            points[0] = pos + dir * size;
            Vec2 up = Vec2::Create(dir.y, -dir.x);
            points[1] = pos + up * (size * 0.5f);
            points[2] = points[1] - up * size;
            DrawSolidPolygon(color, points.GetArrayView());
        }
        void DrawBezier(float width, GraphicsUI::LineCap startCap, GraphicsUI::LineCap endCap, GraphicsUI::Color color, Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3)
        {
            auto p10 = p1 - p0;
            auto p21 = p2 - p1;
            auto p32 = p3 - p2;
            auto posAt = [&](float t)
            {
                Vec2 rs;
                float invT = 1.0f - t;
                rs = p0 * (invT*invT*invT) + p1 * (3.0f * (invT*invT) * t) + p2 * (3.0f * invT * (t * t)) + p3 * (t * t * t);
                return rs;
            };
            auto dirAt = [&](float t)
            {
                float invT = 1.0f - t;
                return p10 * (invT * invT * 3.0f) + p21 * (invT*t*6.0f) + p32 * (t * t * 3.0f);
            };
            float estimatedLength = p10.Length() + p21.Length() + p32.Length();
            int segs = Math::Max(2, (int)sqrt(estimatedLength));
            float invSegs = 1.0f / (segs - 1);
            Vec2 pos0 = posAt(0.0f);
            Vec2 dir0 = dirAt(0.0f).Normalize();
            Vec2 dir1 = Vec2::Create(0.0f, 0.0f), pos1 = Vec2::Create(0.0f, 0.0f);
            Vec2 up0 = Vec2::Create(dir0.y, -dir0.x);
            float halfWidth = width * 0.5f;
            Vec2 v0 = pos0 + up0 * halfWidth;
            Vec2 v1 = v0 - up0 * width;
            CoreLib::Array<Vec2, 4> points;
            points.SetSize(4);
            for (int i = 1; i < segs; i++)
            {
                float t = invSegs * i;
                pos1 = posAt(t);
                dir1 = dirAt(t).Normalize();
                Vec2 up1 = Vec2::Create(dir1.y, -dir1.x);
                Vec2 v2 = pos1 + up1 * halfWidth;
                Vec2 v3 = v2 - up1 * width;
                points[0] = v0;
                points[1] = v1;
                points[2] = v3;
                points[3] = v2;
                DrawSolidPolygon(color, points.GetArrayView());
                v0 = v2;
                v1 = v3;
            }
            if (endCap == GraphicsUI::LineCap::Arrow)
            {
                DrawLineCap(color, pos1, dir1, width * 6.0f);
            }
            if (startCap == GraphicsUI::LineCap::Arrow)
            {
                dir0.x = -dir0.x;
                dir0.y = -dir0.y;
                DrawLineCap(color, pos0, dir0, width * 6.0f);
            }
        }
        void DrawSolidPolygon(const GraphicsUI::Color & color, CoreLib::ArrayView<Vec2> points)
        {
            if (IsBufferFull())
                return;
            auto addPoint = [this](Vec2 p)
            {
                UberVertex vtx;
                vtx.x = p.x;
                vtx.y = p.y;
                vtx.inputIndex = primCounter;
                indexStream.Add(vertexStream.Count());
                vertexStream.Add(vtx);
            };
            addPoint(points[0]);
            for (int i = 1; i <= points.Count() / 2; i++)
            {
                addPoint(points[i]);
                if (points.Count() - i > i)
                    addPoint(points[points.Count() - i]);
            }
            indexStream.Add(-1);
            UniformField fields;
            fields.ClipRectX = (unsigned short)clipRect.x;
            fields.ClipRectY = (unsigned short)clipRect.y;
            fields.ClipRectX1 = (unsigned short)clipRect.z;
            fields.ClipRectY1 = (unsigned short)clipRect.w;
            fields.ShaderType = 0;
            fields.InputColor = color;
            uniformFields.Add(fields);
            primCounter++;
        }

        void DrawSolidQuad(const GraphicsUI::Color & color, float x, float y, float x1, float y1)
        {
            if (IsBufferFull())
                return;
            indexStream.Add(vertexStream.Count());
            indexStream.Add(vertexStream.Count() + 1);
            indexStream.Add(vertexStream.Count() + 2);
            indexStream.Add(vertexStream.Count() + 3);
            indexStream.Add(-1);

            UberVertex points[4];
            points[0].x = x; points[0].y = y; points[0].inputIndex = primCounter;
            points[1].x = x; points[1].y = y1; points[1].inputIndex = primCounter;
            points[2].x = x1; points[2].y = y; points[2].inputIndex = primCounter;
            points[3].x = x1; points[3].y = y1; points[3].inputIndex = primCounter;
            vertexStream.AddRange(points, 4);

            UniformField fields;
            fields.ClipRectX = (unsigned short)clipRect.x;
            fields.ClipRectY = (unsigned short)clipRect.y;
            fields.ClipRectX1 = (unsigned short)clipRect.z;
            fields.ClipRectY1 = (unsigned short)clipRect.w;
            fields.ShaderType = 0;
            fields.InputColor = color;
            uniformFields.Add(fields);
            primCounter++;
        }
        void DrawTextureQuad(Texture2D* /*texture*/, float /*x*/, float /*y*/, float /*x1*/, float /*y1*/)
        {
            /*Vec4 vertexData[4];
            vertexData[0] = Vec4::Create(x, y, 0.0f, 0.0f);
            vertexData[1] = Vec4::Create(x, y1, 0.0f, -1.0f);
            vertexData[2] = Vec4::Create(x1, y1, 1.0f, -1.0f);
            vertexData[3] = Vec4::Create(x1, y, 1.0f, 0.0f);
            vertexBuffer.SetData(vertexData, sizeof(float) * 16);

            textureProgram.Use();
            textureProgram.SetUniform(0, orthoMatrix);
            textureProgram.SetUniform(2, 0);
            textureProgram.SetUniform(3, clipRect);

            glContext->UseTexture(0, texture, linearSampler);
            glContext->BindVertexArray(posUvVertexArray);
            glContext->DrawArray(GL::PrimitiveType::TriangleFans, 0, 4);*/
        }
        void DrawTextQuad(BakedText * text, const GraphicsUI::Color & fontColor, float x, float y, float x1, float y1)
        {
            static int64_t useStamp = 0;
            useStamp++;
            if (IsBufferFull())
                return;
            if (!text->textBuffer && text->Height > 0)
            {
                do
                {
                    text->Rebake();
                    // kick out last used text from cache
                    if (!text->textBuffer && text->Height > 0 && text->Width > 0)
                    {
                        BakedText* victim = nullptr;
                        int64_t minTimeStamp = 0xFFFFFFFFFFFF;
                        for (auto ftext : text->system->bakedTexts)
                        {
                            if (ftext->textBuffer)
                            {
                                if (ftext->lastUse < minTimeStamp)
                                {
                                    minTimeStamp = ftext->lastUse;
                                    victim = ftext;
                                }
                            }
                        }
                        if (!victim)
                        {
                            break;
                        }
                        victim->system->FreeTextBuffer(victim->textBuffer, victim->BufferSize);
                        victim->textBuffer = nullptr;
                        victim->BufferSize = 0;
                    }
                } while (!text->textBuffer && text->Height > 0 && text->Width > 0);
            }
            text->lastUse = useStamp;
            indexStream.Add(vertexStream.Count());
            indexStream.Add(vertexStream.Count() + 1);
            indexStream.Add(vertexStream.Count() + 2);
            indexStream.Add(vertexStream.Count() + 3);
            indexStream.Add(-1);

            UberVertex vertexData[4];
            vertexData[0].x = x; vertexData[0].y = y; vertexData[0].u = 0.0f; vertexData[0].v = 0.0f; vertexData[0].inputIndex = primCounter;
            vertexData[1].x = x; vertexData[1].y = y1; vertexData[1].u = 0.0f; vertexData[1].v = 1.0f; vertexData[1].inputIndex = primCounter;
            vertexData[2].x = x1; vertexData[2].y = y; vertexData[2].u = 1.0f; vertexData[2].v = 0.0f; vertexData[2].inputIndex = primCounter;
            vertexData[3].x = x1; vertexData[3].y = y1; vertexData[3].u = 1.0f; vertexData[3].v = 1.0f; vertexData[3].inputIndex = primCounter;
            vertexStream.AddRange(vertexData, 4);

            UniformField fields;
            fields.ClipRectX = (unsigned short)clipRect.x;
            fields.ClipRectY = (unsigned short)clipRect.y;
            fields.ClipRectX1 = (unsigned short)clipRect.z;
            fields.ClipRectY1 = (unsigned short)clipRect.w;
            fields.ShaderType = 1;
            fields.InputColor = fontColor;
            fields.TextParams.TextWidth = text->Width;
            fields.TextParams.TextHeight = text->Height;
            fields.TextParams.StartPointer = system->GetTextBufferRelativeAddress(text->textBuffer);
            uniformFields.Add(fields);
            primCounter++;
        }
        void DrawRectangleShadow(const GraphicsUI::Color & color, float x, float y, float w, float h, float offsetX, float offsetY, float shadowSize)
        {
            if (IsBufferFull())
                return;
            indexStream.Add(vertexStream.Count());
            indexStream.Add(vertexStream.Count() + 1);
            indexStream.Add(vertexStream.Count() + 2);
            indexStream.Add(vertexStream.Count() + 3);
            indexStream.Add(-1);

            UberVertex vertexData[4];
            vertexData[0].x = x + offsetX - shadowSize * 1.5f; vertexData[0].y = y + offsetY - shadowSize * 1.5f; vertexData[0].u = 0.0f; vertexData[0].v = 0.0f; vertexData[0].inputIndex = primCounter;
            vertexData[1].x = x + offsetX - shadowSize * 1.5f; vertexData[1].y = (y + h + offsetY) + shadowSize * 1.5f; vertexData[1].u = 0.0f; vertexData[1].v = 1.0f; vertexData[1].inputIndex = primCounter;
            vertexData[2].x = x + w + offsetX + shadowSize * 1.5f; vertexData[2].y = y + offsetY - shadowSize * 1.5f; vertexData[2].u = 1.0f; vertexData[2].v = 0.0f; vertexData[2].inputIndex = primCounter;
            vertexData[3].x = x + w + offsetX + shadowSize * 1.5f; vertexData[3].y = (y + h + offsetY) + shadowSize * 1.5f; vertexData[3].u = 1.0f; vertexData[3].v = 1.0f; vertexData[3].inputIndex = primCounter;
            vertexStream.AddRange(vertexData, 4);

            UniformField fields;
            fields.ClipRectX = (unsigned short)x;
            fields.ClipRectY = (unsigned short)y;
            fields.ClipRectX1 = (unsigned short)(x + w);
            fields.ClipRectY1 = (unsigned short)(y + h);
            fields.ShaderType = 2;
            fields.InputColor = color;
            fields.ShadowParams.ShadowOriginX = (unsigned short)(x + offsetX);
            fields.ShadowParams.ShadowOriginY = (unsigned short)(y + offsetY);
            fields.ShadowParams.ShadowWidth = (unsigned short)(w);
            fields.ShadowParams.ShadowHeight = (unsigned short)(h);
            fields.ShadowParams.ShadowSize = (int)(shadowSize * 0.5f);
            uniformFields.Add(fields);
            primCounter++;
        }
        void SetClipRect(float x, float y, float x1, float y1)
        {
            clipRect.x = x;
            clipRect.y = y;
            clipRect.z = x1;
            clipRect.w = y1;
        }
    };

    class UIImage : public GraphicsUI::IImage
    {
    public:
        UISystemBase * context = nullptr;
        CoreLib::RefPtr<Texture2D> texture;
        int w, h;
        UIImage(UISystemBase* ctx, const CoreLib::Imaging::Bitmap & bmp)
        {
            context = ctx;
            texture = context->rendererApi->CreateTexture2D("uiImage", TextureUsage::Sampled, bmp.GetWidth(), bmp.GetHeight(), 1, StorageFormat::RGBA_8);
            texture->SetData(bmp.GetWidth(), bmp.GetHeight(), 1, DataType::Byte4, bmp.GetPixels());
            w = bmp.GetWidth();
            h = bmp.GetHeight();
        }
        virtual int GetHeight() override
        {
            return h;
        }
        virtual int GetWidth() override
        {
            return w;
        }
    };

    GraphicsUI::IImage * UISystemBase::CreateImageObject(const CoreLib::Imaging::Bitmap & bmp)
    {
        return new UIImage(this, bmp);
    }

    Vec4 UISystemBase::ColorToVec(GraphicsUI::Color c)
    {
        return Vec4::Create(c.R / 255.0f, c.G / 255.0f, c.B / 255.0f, c.A / 255.0f);
    }


    void UIWindowContext::TickTimerTick()
    {
        uiEntry->DoTick();
    }

    void UIWindowContext::HoverTimerTick()
    {
        uiEntry->DoMouseHover();
    }

    UISystemBase::UISystemBase(GameEngine::HardwareRenderer * ctx)
    {
        rendererApi = ctx;
        auto textBufferStructInfo = BufferStructureInfo(sizeof(uint32_t), TextBufferSize / sizeof(uint32_t));
        textBufferObj = ctx->CreateMappedBuffer(BufferUsage::StorageBuffer, TextBufferSize, &textBufferStructInfo);
        textBuffer = (unsigned char*)textBufferObj->Map();
        textBufferPool.Init(textBuffer, Log2TextBufferBlockSize, TextBufferSize >> Log2TextBufferBlockSize);
        uiRenderer = new GLUIRenderer(this, ctx);
    }

    UISystemBase::~UISystemBase()
    {
        textBufferObj->Unmap();
        fonts = decltype(fonts)();
        delete uiRenderer;
    }

    void UISystemBase::WaitForDrawFence()
    {
        if (textBufferFence)
            textBufferFence->Wait();
    }

    unsigned char * UISystemBase::AllocTextBuffer(int size)
    {
        return textBufferPool.Alloc(size);
    }

    void UISystemBase::TransferDrawCommands(UIWindowContext * ctx, CoreLib::List<GraphicsUI::DrawCommand>& commands)
    {
        const int MaxEllipseEdges = 32;
        uiRenderer->BeginUIDrawing();
        uiRenderer->SetBufferLimit(ctx->vertexBufferSize / sizeof(UberVertex), ctx->indexBufferSize / sizeof(int),
            ctx->primitiveBufferSize / sizeof(UniformField));

        int ptr = 0;
        while (ptr < commands.Count())
        {
            auto & cmd = commands[ptr];
            switch (cmd.Name)
            {
            case GraphicsUI::DrawCommandName::ClipQuad:
                uiRenderer->SetClipRect(cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                break;
            case GraphicsUI::DrawCommandName::Line:
            {
                uiRenderer->DrawLine(cmd.LineParams.color, cmd.LineParams.width, cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                if (cmd.LineParams.startCap != GraphicsUI::LineCap::None)
                    uiRenderer->DrawLineCap(cmd.LineParams.color, Vec2::Create(cmd.x0, cmd.y0), (Vec2::Create(cmd.x0, cmd.y0) - Vec2::Create(cmd.x1, cmd.y1)).Normalize(), cmd.LineParams.width * 6.0f);
                if (cmd.LineParams.endCap != GraphicsUI::LineCap::None)
                    uiRenderer->DrawLineCap(cmd.LineParams.color, Vec2::Create(cmd.x1, cmd.y1), (Vec2::Create(cmd.x1, cmd.y1) - Vec2::Create(cmd.x0, cmd.y0)).Normalize(), cmd.LineParams.width * 6.0f);
                break;
            }
            case GraphicsUI::DrawCommandName::SolidQuad:
                uiRenderer->DrawSolidQuad(cmd.SolidColorParams.color, cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                break;
            case GraphicsUI::DrawCommandName::TextQuad:
                uiRenderer->DrawTextQuad((BakedText*)cmd.TextParams.text, cmd.TextParams.color, cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                break;
            case GraphicsUI::DrawCommandName::ShadowQuad:
                uiRenderer->DrawRectangleShadow(cmd.ShadowParams.color, (float)cmd.ShadowParams.x, (float)cmd.ShadowParams.y, (float)cmd.ShadowParams.w,
                    (float)cmd.ShadowParams.h, (float)cmd.ShadowParams.offsetX, (float)cmd.ShadowParams.offsetY, cmd.ShadowParams.shadowSize);
                break;
            case GraphicsUI::DrawCommandName::TextureQuad:
                uiRenderer->DrawTextureQuad(((UIImage*)cmd.TextParams.text)->texture.Ptr(), cmd.x0, cmd.y0, cmd.x1, cmd.y1);
                break;
            case GraphicsUI::DrawCommandName::Triangle:
            {
                Array<Vec2, 3> verts;
                verts.Add(Vec2::Create(cmd.x0, cmd.y0));
                verts.Add(Vec2::Create(cmd.x1, cmd.y1));
                verts.Add(Vec2::Create(cmd.TriangleParams.x2, cmd.TriangleParams.y2));
                uiRenderer->DrawSolidPolygon(cmd.TriangleParams.color, verts.GetArrayView());
                break;
            }
            case GraphicsUI::DrawCommandName::Ellipse:
            {
                Array<Vec2, MaxEllipseEdges> verts;
                int edges = Math::Clamp((int)sqrt(Math::Max(cmd.x1 - cmd.x0, cmd.y1 - cmd.y0)) * 4, 6, verts.GetCapacity());
                float dTheta = Math::Pi * 2.0f / edges;
                float theta = 0.0f;
                float dotX = (cmd.x0 + cmd.x1) * 0.5f;
                float dotY = (cmd.y0 + cmd.y1) * 0.5f;
                float radX = (cmd.x1 - cmd.x0) * 0.5f;
                float radY = (cmd.y1 - cmd.y0) * 0.5f;
                for (int i = 0; i < edges; i++)
                {
                    verts.Add(Vec2::Create(dotX + radX * cos(theta), dotY - radY * sin(theta)));
                    theta += dTheta;
                }
                uiRenderer->DrawSolidPolygon(cmd.SolidColorParams.color, verts.GetArrayView());
                break;
            }
            case GraphicsUI::DrawCommandName::Arc:
            {
                int totalEdges = Math::Clamp((int)sqrt(Math::Max(cmd.x1 - cmd.x0, cmd.y1 - cmd.y0)) * 4, 6, MaxEllipseEdges);
                float thetaDelta = Math::Pi / totalEdges * 2.0f;
                float theta = cmd.ArcParams.angle1;
                float dotX = (cmd.x0 + cmd.x1) * 0.5f;
                float dotY = (cmd.y0 + cmd.y1) * 0.5f;
                float radX = (cmd.x1 - cmd.x0) * 0.5f;
                float radY = (cmd.y1 - cmd.y0) * 0.5f;
                Vec2 pos = Vec2::Create(dotX + radX * cos(theta), dotY - radY * sin(theta));
                Vec2 normal = Vec2::Create(radY * cos(theta), -radX * sin(theta)).Normalize();
                Array<Vec2, 4> verts;
                verts.SetSize(4);
                while (theta < cmd.ArcParams.angle2)
                {
                    theta += thetaDelta;
                    theta = Math::Min(theta, cmd.ArcParams.angle2);
                    Vec2 pos2 = Vec2::Create(dotX + radX * cos(theta), dotY - radY * sin(theta));
                    Vec2 normal2 = Vec2::Create(radY * cos(theta), -radX * sin(theta)).Normalize();
                    verts[0] = pos;
                    verts[1] = pos + normal * cmd.ArcParams.width;
                    verts[2] = pos2 + normal2 * cmd.ArcParams.width;
                    verts[3] = pos2;
                    pos = pos2;
                    normal = normal2;
                    uiRenderer->DrawSolidPolygon(cmd.ArcParams.color, verts.GetArrayView());
                }
                break;
            }
            case GraphicsUI::DrawCommandName::Bezier:
            {
                uiRenderer->DrawBezier(cmd.BezierParams.width,
                    cmd.BezierParams.startCap, cmd.BezierParams.endCap,
                    cmd.BezierParams.color,
                    Vec2::Create(cmd.x0, cmd.y0),
                    Vec2::Create(cmd.BezierParams.cx0, cmd.BezierParams.cy0),
                    Vec2::Create(cmd.BezierParams.cx1, cmd.BezierParams.cy1),
                    Vec2::Create(cmd.x1, cmd.y1));
            }
            break;
            }
            ptr++;
        }
        uiRenderer->EndUIDrawing(ctx);
    }

    void UISystemBase::QueueDrawCommands(Texture2D* baseTexture, UIWindowContext* ctx, WindowBounds viewport, Fence* frameFence)
    {
        textBufferFence = frameFence;
        uiRenderer->SubmitCommands(baseTexture, viewport, ctx);
    }

    GameEngine::FrameBuffer * UISystemBase::CreateFrameBuffer(GameEngine::Texture2D * texture)
    {
        return uiRenderer->CreateFrameBuffer(texture);
    }

    void UISystemBase::UnregisterWindowContext(UIWindowContext * ctx)
    {
        windowContexts.Remove(ctx->window);
    }

    RefPtr<UIWindowContext> UISystemBase::CreateWindowContext(SystemWindow* handle, int w, int h, int log2BufferSize)
    {
        RefPtr<UIWindowContext> rs = new UIWindowContext();
        rs->window = handle;
        
        rs->sysInterface = this;
        rs->hwRenderer = rendererApi;
        rs->surface = rendererApi->CreateSurface(handle->GetNativeHandle(), w, h);

        rs->uiEntry = new GraphicsUI::UIEntry(w, h, rs.Ptr(), this);
        rs->uniformBuffer = rendererApi->CreateMappedBuffer(BufferUsage::UniformBuffer, sizeof(VectorMath::Matrix4));
        rs->cmdBuffer = new AsyncCommandBuffer(rendererApi);
        rs->primitiveBufferSize = 1 << log2BufferSize;
        rs->vertexBufferSize = rs->primitiveBufferSize / sizeof(UniformField) * sizeof(UberVertex) * 16;
        rs->indexBufferSize = rs->vertexBufferSize >> 2;
        auto primitiveBufferStructInfo = BufferStructureInfo(
            sizeof(uint32_t) * 4, rs->primitiveBufferSize / (sizeof(uint32_t) * 4) * DynamicBufferLengthMultiplier);
        rs->primitiveBuffer = rendererApi->CreateMappedBuffer(BufferUsage::StorageBuffer,
            rs->primitiveBufferSize * DynamicBufferLengthMultiplier, &primitiveBufferStructInfo);
        rs->vertexBuffer = rendererApi->CreateMappedBuffer(BufferUsage::ArrayBuffer, rs->vertexBufferSize * DynamicBufferLengthMultiplier);
        rs->indexBuffer = rendererApi->CreateMappedBuffer(BufferUsage::IndexBuffer, rs->indexBufferSize * DynamicBufferLengthMultiplier);

        for (int i = 0; i < rs->descSets.GetCapacity(); i++)
        {
            auto descSet = rendererApi->CreateDescriptorSet(uiRenderer->GetDescLayout());
            descSet->BeginUpdate();
            descSet->Update(0, rs->uniformBuffer.Ptr());
            descSet->Update(1, rs->primitiveBuffer.Ptr(), i * rs->primitiveBufferSize, rs->primitiveBufferSize);
            descSet->Update(2, textBufferObj.Ptr());
            descSet->EndUpdate();
            rs->descSets.Add(descSet);
        }
        rs->SetSize(w, h);
        windowContexts[rs->window] = rs.Ptr();
        return rs;
    }
}