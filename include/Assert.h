#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstdarg>

#define ASSERT

#ifdef ASSERT

	inline void AssertFail(const char* expr, const char* file, int line, const char* func, const char* fmt = nullptr, ...)
	{
	    std::fprintf(stderr, "[ASSERTION FAILED]\n");
	    std::fprintf(stderr, "  Expression: %s\n", expr);
	    std::fprintf(stderr, "  Location:   %s:%d   (%s)\n", file, line, func);

	    if (fmt)
	    {
	        std::fprintf(stderr, "  Message:    ");
	        va_list args;
	        va_start(args, fmt);
	        std::vfprintf(stderr, fmt, args);
	        va_end(args);
	        std::fprintf(stderr, "\n");
	    }

	    std::fflush(stderr);
	    std::abort();
}

#define Assert(expr, ...) \
    ((expr) \
    ? static_cast<void>(0) \
    : AssertFail(#expr, __FILE__, __LINE__, __func__, ##__VA_ARGS__))

#else

	#define Assert(expr, ...) ((void)0)

#endif
