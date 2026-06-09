#pragma once

#include <libsodium/sodium.h>

#include "Types.hpp"

#include <array>
#include <string>

using UUID = std::array<unsigned char, 16>;

inline constexpr UUID NULL_UUID = {};

namespace std
{
	template <>
	struct hash<std::array<unsigned char, 16>>
	{
		u64 operator()(const std::array<unsigned char, 16>& uuid) const noexcept
		{
			u64 hash = 0;
			for (const auto b : uuid)
			{
				hash = (hash * 31) ^ b;
			}

			return hash;
		}
	};
}

extern unsigned char s_defaultKey[crypto_secretbox_KEYBYTES];

namespace Encryption
{
	// Encryption and decryption
	bool InitEncryption();

	std::string Encrypt(const std::string& data, const std::string& key);
	std::string Decrypt(const std::string& encryptedData, const std::string& key);

	// With defualt key
	std::string Encrypt(const std::string& data);
	std::string Decrypt(const std::string& encryptedData);

	UUID GenerateUUID();
}