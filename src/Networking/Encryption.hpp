#pragma once

#include <libsodium/sodium.h>

#include "Types.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

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
	bool InitEncryption();

	std::vector<std::byte> Encrypt(const std::byte* data, const u32 dataSize, const std::vector<std::byte>& key);
	std::vector<std::byte> Decrypt(const std::byte* encryptedData, const u32 encryptedSize,
	const std::vector<std::byte>& key);

	std::vector<std::byte> Encrypt(const std::byte* data, const u32 dataSize);
	std::vector<std::byte> Decrypt(const std::byte* encryptedData, const u32 encryptedSize);

	UUID GenerateUUID();
}