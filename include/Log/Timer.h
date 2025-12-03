#pragma once

#include "Log.h"

#include "Core/Types.h"

#include <deque>

class Timer
{
public:

    Timer()
    {

    }

    Timer(const std::string& nameIn)
    {
        name = nameIn;
        start = std::chrono::steady_clock::now();
    }

    ~Timer()
    {
        if (!done)
        {
            Stop();
        }
    }

    inline void Start()
    {
        Start("");
    }

    inline void Start(const std::string& nameIn)
    {
        Stop();

        name = nameIn;
        start = std::chrono::steady_clock::now();

        done = false;
    }

    inline double Stop()
    {
        return Stop("");
    }

    inline double Stop(const std::string& text)
    {
        if (done)
        {
            return 0.0;
        }

        end = std::chrono::steady_clock::now();
        done = true;

        double microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        if (name.size())
        {
            Log(name, " took: ", microseconds * 0.001, " ms ", text);
        }
        
        return microseconds * 0.001;
    }

private:

    std::chrono::time_point<std::chrono::steady_clock> start;
    std::chrono::time_point<std::chrono::steady_clock> end;

    bool done = true;

    std::string name;
};

template <typename T>
class RollingAverage
{
public:

    RollingAverage(const u64 samples = 100)
    {
        _sampleCount = samples;
    }

    inline void Add(const T value)
    {
        _samples.push_back(value);

        if (_samples.size() > _sampleCount)
        {
            _samples.pop_front();
        }
    }

    inline void operator+=(const T value)
    {
        Add(value);
    }

    inline T Average() const
    {
        if (_samples.empty())
        {
            return 0;
        }

        T sum = 0;

        for (const float value : _samples)
        {
            sum += value;
        }

        return sum / _samples.size();
    }

private:

    std::deque<T> _samples;
    u64 _sampleCount;
};