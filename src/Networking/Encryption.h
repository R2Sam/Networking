#pragma once

#include <libsodium/sodium.h>

#include "Types.h"

#include <string>
#include <array>

using UUID = std::array<unsigned char, 16>;

namespace std
{
    template<>
    struct hash<std::array<unsigned char, 16>>
    {
        std::size_t operator()(const std::array<unsigned char, 16>& uuid) const noexcept
        {
            std::size_t h = 0;
            for (auto b : uuid)
            {
                h = (h * 31) ^ b;
            }
            
            return h;
        }
    };
}

extern unsigned char DefaultKey[crypto_secretbox_KEYBYTES];

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