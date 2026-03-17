#pragma once
#include <chrono>
#include <cxxabi.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <typeindex>

inline std::mutex s_timeMutex;

inline void GetThreadSafeLocalTime(const time_t& timeInput, std::tm& timeInfo)
{
	std::lock_guard<std::mutex> lock(s_timeMutex);
	std::tm* result = localtime(&timeInput);
	if (result != nullptr)
	{
		timeInfo = *result;
	}
}

inline std::string GetCurrentTimeString()
{
	auto now = std::chrono::system_clock::now();
	auto inTimeT = std::chrono::system_clock::to_time_t(now);
	auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

	std::tm timeInfo;
	std::stringstream ss;

	GetThreadSafeLocalTime(inTimeT, timeInfo);

	char buffer[9];
	strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeInfo);

	ss << buffer << '.' << std::setw(3) << std::setfill('0') << static_cast<int>(milliseconds.count());

	return ss.str();
}

inline const char* const ANSI_RESET = "\033[0m";
inline const char* const ANSI_SAVE_CURSOR = "\033[s";
inline const char* const ANSI_LOAD_CURSOR = "\033[u";
inline const char* const ANSI_HIDE_CURSOR = "\033[?25l";
inline const char* const ANSI_SHOW_CURSOR = "\033[?25h";
inline const char* const ANSI_CLEAR_LINE = "\033[2K";
inline const char* const ANSI_SCROLL_DOWN = "\033[D";

inline const char* const LOG_GREEN = "\033[92m";
inline const char* const LOG_BLUE = "\033[94m";
inline const char* const LOG_YELLOW = "\033[93m";
inline const char* const LOG_RED = "\033[91m";
inline const char* const LOG_WHITE = "\033[97m";

template <typename, typename = void>
struct HasOstreamOperator : std::false_type
{
};

template <typename T>
struct HasOstreamOperator<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
: std::true_type
{
};

template <typename, typename = void>
struct HasPrintMethod : std::false_type
{
};

template <typename T>
struct HasPrintMethod<T, std::void_t<decltype(std::declval<const T&>().Print())>> : std::true_type
{
};

template <typename T>
std::string CheckOperator(const T& object)
{
	if constexpr (HasOstreamOperator<T>::value)
	{
		std::ostringstream oss;
		oss << object;
		return oss.str();
	}

	else if constexpr (HasPrintMethod<T>::value)
	{
		return object.Print();
	}

	return "INVALID";
}

template <typename... Args>
void Log(Args&&... args)
{
#ifdef LOGFILE
	std::ofstream logFile("log.txt", std::ios_base::app);
	logFile << "[" << GetCurrentTimeString() << "] ";
	(logFile << ... << CheckOperator(args)) << '\n';
#endif

	std::cerr << LOG_WHITE << "[" << GetCurrentTimeString() << "] ";
	(std::cerr << ... << CheckOperator(args)) << ANSI_RESET << '\n';
}

template <typename... Args>
void LogColor(const char* color, Args&&... args)
{
#ifdef LOGFILE
	std::ofstream logFile("log.txt", std::ios_base::app);
	logFile << "[" << GetCurrentTimeString() << "] ";
	(logFile << ... << CheckOperator(args)) << '\n';
#endif

	std::cerr << LOG_WHITE << "[" << GetCurrentTimeString() << "] ";
	std::cerr << color;
	(std::cerr << ... << CheckOperator(args));
	std::cerr << ANSI_RESET << '\n';
}

template <typename... Args>
void Output(Args&&... args)
{
	(std::cout << ... << CheckOperator(args)) << ANSI_RESET << '\n';
}

template <typename... Args>
void OutputColor(const char* color, Args&&... args)
{
	std::cout << color;
	(std::cout << ... << CheckOperator(args));
	std::cout << ANSI_RESET << '\n';
}

template <typename... Args>
void OutputErr(Args&&... args)
{
	(std::cerr << ... << CheckOperator(args)) << ANSI_RESET << '\n';
}

template <typename... Args>
void OutputErrColor(const char* color, Args&&... args)
{
	std::cerr << color;
	(std::cerr << ... << CheckOperator(args));
	std::cerr << ANSI_RESET << '\n';
}

#ifdef _WIN32

#ifndef _INC_WINDOWS

extern "C"
{
	__declspec(dllimport) void* __stdcall GetStdHandle(unsigned long);
	__declspec(dllimport) int __stdcall GetConsoleMode(void*, unsigned long*);
	__declspec(dllimport) int __stdcall SetConsoleMode(void*, unsigned long);
	__declspec(dllimport) int __stdcall SetConsoleTextAttribute(void*, unsigned short);
}

#endif

class Ansi
{

public:

	Ansi()
	{
		void* hOutput = GetStdHandle(-11);
		unsigned long outMode;
		GetConsoleMode(hOutput, &outMode);
		SetConsoleMode(hOutput, outMode | 0x0004);

		void* hError = GetStdHandle(-12);
		unsigned long errMode;
		GetConsoleMode(hError, &errMode);
		SetConsoleMode(hError, errMode | 0x0004);
	}

	~Ansi()
	{
		void* hOutput = GetStdHandle(-11);
		unsigned long outMode;
		GetConsoleMode(hOutput, &outMode);
		SetConsoleMode(hOutput, outMode & ~0x0004);

		void* hError = GetStdHandle(-12);
		unsigned long errMode;
		GetConsoleMode(hError, &errMode);
		SetConsoleMode(hError, errMode & ~0x0004);
	}
};

inline Ansi ansi;

#endif

template <typename T>
std::string Demangle()
{
	std::string name = typeid(T).name();

	int status = -4;

	std::unique_ptr<char, void (*)(void*)> res{abi::__cxa_demangle(name.c_str(), NULL, NULL, &status), std::free};

	return (status == 0) ? res.get() : name;
}

inline std::string Demangle(const std::type_index& type)
{
	const char* name = type.name();

	int status = -4;

	std::unique_ptr<char, void (*)(void*)> res{abi::__cxa_demangle(name, nullptr, nullptr, &status), std::free};

	return (status == 0) ? res.get() : name;
}

template <typename T>
std::string DemangleWithoutNamespace()
{
	std::string demangled = Demangle<T>();

	size_t pos = demangled.find_last_of("::");
	if (pos != std::string::npos)
	{
		return demangled.substr(pos + 1);
	}

	return demangled;
}

inline std::string DemangleWithoutNamespace(const std::type_index& type)
{
	std::string demangled = Demangle(type);

	size_t pos = demangled.find_last_of("::");
	if (pos != std::string::npos)
	{
		return demangled.substr(pos + 1);
	}

	return demangled;
}
