#include "PerformanceCounter.h"

namespace CoreLib
{
	namespace Diagnostics
	{
#ifdef USE_WIN32_TIMER
		TimePoint PerformanceCounter::frequency = 0;
#endif
	}
}
