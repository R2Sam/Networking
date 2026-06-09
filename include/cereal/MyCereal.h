#pragma once

#include "cereal/archives/binary.hpp"

#include <string>
#include <sstream>
#include <vector>

template <class T>
concept CerealSerializable =
requires(cereal::BinaryOutputArchive& arOut, cereal::BinaryInputArchive& arIn, const T& obj)
{
    arOut(obj);
    arIn(obj);
};

template<CerealSerializable Object>
std::vector<std::byte> Serialize(const Object& object)
{
    std::ostringstream oss(std::ios::binary);
    cereal::BinaryOutputArchive archive(oss);

    archive(object);

    std::string str = std::move(oss).str();

    std::vector<std::byte> bytes(reinterpret_cast<std::byte*>(str.data()), reinterpret_cast<std::byte*>(str.data() + str.size()));

    return bytes;
}

template<CerealSerializable Object>
Object Deserialize(const std::vector<std::byte>& bytes)
{
    std::string str(reinterpret_cast<const char*>(bytes.data()), bytes.size());

    std::istringstream iss(std::move(str), std::ios::binary);
    cereal::BinaryInputArchive archive(iss);

    Object object;
    archive(object);

    return object;
}