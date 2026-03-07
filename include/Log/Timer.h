#pragma once

#include "Log.h"

#include "Types.h"

#include <deque>

class Timer
{
public:

	Timer()
	{
	}

	Timer(const std::string& name)
	{
		m_name = name;
		m_start = std::chrono::steady_clock::now();
	}

	~Timer()
	{
		if (!m_done)
		{
			Stop();
		}
	}

	void Start()
	{
		Start("");
	}

	void Start(const std::string& name)
	{
		Stop();

		m_name = name;
		m_start = std::chrono::steady_clock::now();

		m_done = false;
	}

	double Stop()
	{
		return Stop("");
	}

	double Stop(const std::string& text)
	{
		if (m_done)
		{
			return 0.0;
		}

		m_end = std::chrono::steady_clock::now();
		m_done = true;

		double microseconds = std::chrono::duration_cast<std::chrono::microseconds>(m_end - m_start).count();
		if (!m_name.empty())
		{
			Log(m_name, " took: ", microseconds * 0.001, " ms ", text);
		}

		return microseconds * 0.001;
	}

private:

	std::chrono::time_point<std::chrono::steady_clock> m_start;
	std::chrono::time_point<std::chrono::steady_clock> m_end;

	bool m_done = true;

	std::string m_name;
};

template <typename T>
class RollingAverage
{
public:

	RollingAverage(const u64 samples = 100)
	{
		m_sampleCount = samples;
	}

	void Add(const T value)
	{
		m_samples.push_back(value);

		if (m_samples.size() > m_sampleCount)
		{
			m_samples.pop_front();
		}
	}

	void operator+=(const T value)
	{
		Add(value);
	}

	T Average() const
	{
		if (m_samples.empty())
		{
			return 0;
		}

		T sum = 0;

		for (const float value : m_samples)
		{
			sum += value;
		}

		return sum / m_samples.size();
	}

private:

	std::deque<T> m_samples;
	u64 m_sampleCount;
};