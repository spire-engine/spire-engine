#ifndef CORE_LIB_DEBUG_ASSERT_H
#define CORE_LIB_DEBUG_ASSERT_H

namespace CoreLib
{
    namespace Diagnostics
    {
        void DynamicAssert(const char * message, bool condition);
    }
}

#endif