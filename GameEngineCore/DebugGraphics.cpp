#include "DebugGraphics.h"
#include "Drawable.h"
#include "Mesh.h"
#include "MeshBuilder.h"

namespace GameEngine
{
    using namespace CoreLib;

    class DebugGraphicsImpl : public DebugGraphics
    {
    private:
        RefPtr<Drawable> linesDrawable;
        RefPtr<Drawable> trianglesDrawable;
    public:
        virtual void Clear() override
        {
        }
        virtual void AddLine(VectorMath::Vec4 color, VectorMath::Vec3 v0, VectorMath::Vec3 v1) override
        {
        }
        virtual void AddTriangle(VectorMath::Vec4 color, VectorMath::Vec3 v0, VectorMath::Vec3 v1, VectorMath::Vec3 v2) override
        {
        }
        virtual CoreLib::ArrayView<Drawable*> GetDrawables() override
        {

        }
    };

    DebugGraphics * CreateDebugGraphics()
    {
        return new DebugGraphicsImpl();
    }
}