#pragma once

#include <cereal/archives/binary.hpp>

#include <string>
#include <sstream>
#include <vector>

template<typename T>
std::vector<std::byte> Serialize(const T& object)
{
	std::ostringstream oss(std::ios::binary);
	{
	    cereal::BinaryOutputArchive archive(oss);
	    archive(object);
	}

	std::string str = oss.str();

	std::byte* ptr = reinterpret_cast<std::byte*>(str.data());
	return std::vector<std::byte>(ptr, ptr + str.size());
}

template<typename T>
T Deserialize(const std::vector<std::byte>& data)
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