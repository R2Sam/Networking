#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#define ASSERT

#ifdef ASSERT

template <typename... Args>
inline void AssertFail(const char* expr, const char* file, int line, const char* func, Args&&... args)
{
	std::fprintf(stderr, "[ASSERTION FAILED]\n");
	std::fprintf(stderr, "  Expression: %s\n", expr);
	std::fprintf(stderr, "  Location:   %s:%d   (%s)\n", file, line, func);

	if constexpr (sizeof...(args) > 0)
	{
		std::cerr << "  Message:    ";
		(std::cerr << ... << std::forward<Args>(args));
		std::cerr << '\n';
	}

	std::fflush(stderr);
	std::abort();
}

#define Assert(expr, ...)                                                                                              \
	((expr) ? static_cast<void>(0) : AssertFail(#expr, __FILE__, __LINE__, __func__ __VA_OPT__(, ) __VA_ARGS__))

#else

#define Assert(expr, ...) ((void)0)

#endif
