#pragma once

#include <cereal/archives/binary.hpp>

#include <string>
#include <sstream>
#include <vector>

using u8 = unsigned char;

template<typename T>
std::vector<u8> Serialize(const T& object)
{
	std::ostringstream oss(std::ios::binary);
	{
	    cereal::BinaryOutputArchive archive(oss);
	    archive(object);
	}

	std::string str = oss.str();

	return std::vector<u8>(str.begin(), str.end());
}

template<typename T>
T Deserialize(const std::vector<u8>& data)
{
	T object;

	std::string str((const char*)data.data(), data.size());

	std::istringstream iss(str, std::ios::binary);
	{
	    cereal::BinaryInputArchive archive(iss);
	    archive(object);
	}

	return object;
}