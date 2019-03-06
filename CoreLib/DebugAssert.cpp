#include "DebugAssert.h"
#include "Exception.h"

namespace CoreLib
{
    namespace Diagnostics
    {
        class AssertException : public CoreLib::Basic::Exception
        {
        public:
            AssertException(CoreLib::Basic::String message)
                : Exception(message)
            {}
        };
        void CoreLib::Diagnostics::DynamicAssert(const char * message, bool condition)
        {
            if (!condition)
                throw AssertException(message);
        }

    }
}