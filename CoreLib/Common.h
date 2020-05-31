#ifndef CORE_LIB_COMMON_H
#define CORE_LIB_COMMON_H

#include <cstdint>
#include <assert.h>

#ifdef __GNUC__
#define CORE_LIB_ALIGN_16(x) x __attribute__((aligned(16)))
#else
#define CORE_LIB_ALIGN_16(x) __declspec(align(16)) x
#endif

#define VARIADIC_TEMPLATE

#define CORELIB_ABORT(x) {printf("Fatal error: %s\n", x); abort(); }
#define CORELIB_NOT_IMPLEMENTED(x) {printf("Not impelmented: %s (%s:%d)\n", x, __FILE__, __LINE__); abort(); }
#define CORELIB_UNREACHABLE(x) {printf("Unreachable path executed: %s\n", x); abort(); }
template <typename... Args> inline void corelib_unused_f(Args&&...) {}
#define CORELIB_UNUSED(...) corelib_unused_f(__VA_ARGS__)
#ifdef _DEBUG
#define CORELIB_DEBUG_ASSERT(x) CORELIB_ASSERT(x)
#define CORELIB_ASSERT(x) assert(x)
#else
#define CORELIB_DEBUG_ASSERT(x)
#define CORELIB_ASSERT(x)                                                                                              \
    {                                                                                                                  \
        bool _corelib_assert_result_ = (bool)(x);                                                                      \
        corelib_unused_f(_corelib_assert_result_);                                                                     \
    }
#endif
namespace CoreLib
{
	typedef int64_t Int64;
	typedef unsigned short Word;
#if defined(_M_X64) || defined(__LP64__)
	typedef int64_t PtrInt;
#else
	typedef int PtrInt;
#endif
	namespace Basic
	{
		class Object
		{
		public:
			virtual ~Object()
			{}
		};

		template <typename T>
		inline T&& _Move(T & obj)
		{
			return static_cast<T&&>(obj);
		}

		template <typename T>
		inline void Swap(T & v0, T & v1)
		{
			T tmp = _Move(v0);
			v0 = _Move(v1);
			v1 = _Move(tmp);
		}
	}
}

#endif
