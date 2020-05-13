#ifndef GX_WIN_DEBUG_H
#define GX_WIN_DEBUG_H

#include "../Basic.h"
#ifdef _WIN32
#include <Windows.h>
#endif
namespace CoreLib
{
	namespace Diagnostics
	{
		using namespace CoreLib::Basic;
		class Debug
		{
		public:
			static void Write(const String & text)
			{
#ifdef _WIN32
				if (IsDebuggerPresent() != 0)
				{
					OutputDebugStringW(text.ToWString());
				}
#else
				printf("%s", text.Buffer());
#endif
			}
			static void WriteLine(const String & text)
			{
#ifdef _WIN32
				if (IsDebuggerPresent() != 0)
				{
					OutputDebugStringW(text.ToWString());
					OutputDebugStringW(L"\n");
				}
#else
				printf("%s\n", text.Buffer());
#endif
			}
		};

		class DebugWriter
		{
		public:
			DebugWriter & operator << (const String & text)
			{
				Debug::Write(text);
				return *this;
			}
		};
	}
}

#endif