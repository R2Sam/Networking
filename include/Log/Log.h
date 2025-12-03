#pragma once
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <mutex>

inline std::mutex timeMutex;

inline void GetThreadSafeLocalTime(const time_t& timeInput, std::tm& timeInfo)
{
    std::lock_guard<std::mutex> lock(timeMutex);
    std::tm* result = localtime(&timeInput);
    if (result != nullptr) 
    {
        timeInfo = *result;
    }
}

inline std::string GetCurrentTimeString()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm timeInfo;
    std::stringstream ss;

    GetThreadSafeLocalTime(in_time_t, timeInfo);

    char buffer[9];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeInfo);

    ss << buffer << '.' << std::setw(3) << std::setfill('0') << static_cast<int>(milliseconds.count());

    return ss.str();
}

inline const char* ANSI_RESET = "\033[0m";
inline const char* ANSI_SAVE_CURSOR = "\033[s";
inline const char* ANSI_LOAD_CURSOR = "\033[u";
inline const char* ANSI_HIDE_CURSOR = "\033[?25l";
inline const char* ANSI_SHOW_CURSOR = "\033[?25h";
inline const char* ANSI_CLEAR_LINE = "\033[2K";
inline const char* ANSI_SCROLL_DOWN = "\033[D";

inline const char* LOG_GREEN = "\033[92m";
inline const char* LOG_BLUE = "\033[94m";
inline const char* LOG_YELLOW = "\033[93m";
inline const char* LOG_RED = "\033[91m";
inline const char* LOG_WHITE = "\033[97m";

template<typename... Args>
void Log(Args&&... args)
{
    #ifdef LOGFILE
        std::ofstream logFile("log.txt", std::ios_base::app);
        logFile << "[" << GetCurrentTimeString() << "] ";
        (logFile << ... << std::forward<Args>(args)) << '\n';
    #endif

    std::cerr << LOG_WHITE << "[" << GetCurrentTimeString() << "] ";
    (std::cerr << ... << std::forward<Args>(args)) << ANSI_RESET << '\n';
}

template<typename... Args>
void LogColor(const char* color, Args&&... args)
{
    #ifdef LOGFILE
        std::ofstream logFile("log.txt", std::ios_base::app);
        logFile << "[" << GetCurrentTimeString() << "] ";
        (logFile << ... << std::forward<Args>(args)) << '\n';
    #endif
    
    std::cerr << LOG_WHITE << "[" << GetCurrentTimeString() << "] ";
    std::cerr << color;
    (std::cerr << ... << std::forward<Args>(args));
    std::cerr << ANSI_RESET << '\n';
}

template<typename... Args>
void Output(Args&&... args)
{
    (std::cout << ... << std::forward<Args>(args)) << ANSI_RESET << '\n';
}

template<typename... Args>
void OutputColor(const char* color, Args&&... args)
{
    std::cout << color;
    (std::cout << ... << std::forward<Args>(args));
    std::cout << ANSI_RESET << '\n';
}

template<typename... Args>
void OutputErr(Args&&... args)
{
    (std::cerr << ... << std::forward<Args>(args)) << ANSI_RESET << '\n';
}

template<typename... Args>
void OutputErrColor(const char* color, Args&&... args)
{
    std::cerr << color;
    (std::cerr << ... << std::forward<Args>(args));
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