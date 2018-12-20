#ifndef CORELIB_PERFORMANCE_COUNTER_H
#define CORELIB_PERFORMANCE_COUNTER_H

#include "Common.h"

#ifdef USE_WIN32_TIMER
#define VC_EXTRALEAN
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef VC_EXTRALEAN
#undef WIN32_LEAN_AND_MEAN
#else
#include <chrono>
#endif

namespace CoreLib
{
	namespace Diagnostics
	{
#ifdef USE_WIN32_TIMER
		typedef long long TimePoint;
		typedef long long Duration;
#else
        typedef std::chrono::high_resolution_clock::time_point TimePoint;
        typedef std::chrono::high_resolution_clock::duration Duration;
#endif
		class PerformanceCounter
		{
#ifdef USE_WIN32_TIMER
			static TimePoint frequency;
#endif
        public:
			static inline TimePoint Start() 
			{
#ifdef USE_WIN32_TIMER
				TimePoint rs;
				QueryPerformanceCounter((LARGE_INTEGER*)&rs);
				return rs;
#else
                return std::chrono::high_resolution_clock::now();
#endif
			}
			static inline Duration End(TimePoint counter)
			{
				return Start() - counter;
			}
			static inline float EndSeconds(TimePoint counter)
			{
				return (float)ToSeconds(Start() - counter);
			}
			static inline double ToSeconds(Duration duration)
			{
#ifdef USE_WIN32_TIMER
				if (frequency == 0)
					QueryPerformanceFrequency((LARGE_INTEGER*)&frequency);
				auto rs = duration / (double)frequency;
				return rs;
#else
                return std::chrono::duration<float>(duration).count();
#endif
			}
		};
	}
}

#endif