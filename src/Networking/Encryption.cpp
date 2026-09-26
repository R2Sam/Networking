#include "Encryption.hpp"

#include "Log/Log.hpp"

#include <cstddef>
#include <vector>

static bool s_sodiumInitialized = false;

static constexpr unsigned char DEFAULT_KEY[crypto_secretbox_KEYBYTES] = {0x98, 0x97, 0xf3, 0x7d, 0x8a, 0x1a, 0x61, 0x37,
0x15, 0x14, 0x93, 0xb3, 0x78, 0x46, 0x73, 0x52, 0xd1, 0x93, 0xf1, 0x6a, 0x74, 0x6c, 0x81, 0xa1, 0x35, 0xd8, 0x7d, 0x0b,
0x97, 0x03, 0xcc, 0x36};

unsigned char s_defaultKey[crypto_secretbox_KEYBYTES];

bool Encryption::InitEncryption()
{
	if (!s_sodiumInitialized)
	{
		if (sodium_init() < 0)
		{
			LogColor(LOG_RED, "Failed to initialize encryption");
			return false;
		}

		for (size_t i = 0; i < crypto_secretbox_KEYBYTES; ++i)
		{
			s_defaultKey[i] = DEFAULT_KEY[i];
		}

		s_sodiumInitialized = true;
	}

	return true;
}

std::vector<std::byte> Encryption::Encrypt(const std::byte* data, const u32 dataSize, const std::vector<std::byte>& key)
{
	if (!s_sodiumInitialized)
	{
		return {};
	}

	if (key.size() != crypto_box_BEFORENMBYTES)
	{
		LogColor(LOG_RED, "Bad encryption key");
		return {};
	}

	if (dataSize > 0 && data == nullptr)
	{
		return {};
	}

	const size_t nonceSize = crypto_secretbox_NONCEBYTES;
	const size_t macSize = crypto_secretbox_MACBYTES;
	const size_t size = static_cast<size_t>(dataSize);

	std::vector<std::byte> output(nonceSize + macSize + size);

	randombytes_buf(output.data(), nonceSize);

	if (crypto_box_easy_afternm(reinterpret_cast<unsigned char*>(output.data() + nonceSize),
		reinterpret_cast<const unsigned char*>(data), size, reinterpret_cast<const unsigned char*>(output.data()),
		reinterpret_cast<const unsigned char*>(key.data())))
	{
		LogColor(LOG_RED, "Failed to encrypt with key");
		return {};
	}

	return output;
}

std::vector<std::byte> Encryption::Decrypt(const std::byte* encryptedData, const u32 encryptedSize,
const std::vector<std::byte>& key)
{
	if (!s_sodiumInitialized)
	{
		return {};
	}

	if (key.size() != crypto_box_BEFORENMBYTES)
	{
		LogColor(LOG_RED, "Bad decryption key");
		return {};
	}

	if (encryptedData == nullptr)
	{
		return {};
	}

	const size_t nonceSize = crypto_secretbox_NONCEBYTES;
	const size_t macSize = crypto_secretbox_MACBYTES;
	const size_t size = static_cast<size_t>(encryptedSize);

	if (size < nonceSize + macSize)
	{
		LogColor(LOG_RED, "Ciphertext is too short ", size);
		return {};
	}

	const std::byte* nonce = encryptedData;
	const std::byte* ciphertext = encryptedData + nonceSize;
	const size_t ciphertextSize = size - nonceSize;

	std::vector<std::byte> decrypted(ciphertextSize - macSize);

	if (crypto_box_open_easy_afternm(reinterpret_cast<unsigned char*>(decrypted.data()),
		reinterpret_cast<const unsigned char*>(ciphertext), ciphertextSize,
		reinterpret_cast<const unsigned char*>(nonce), reinterpret_cast<const unsigned char*>(key.data())) != 0)
	{
		LogColor(LOG_RED, "Failed to decrypt with key");
		return {};
	}

	return decrypted;
}

std::vector<std::byte> Encryption::Encrypt(const std::byte* data, const u32 dataSize)
{
	if (!s_sodiumInitialized)
	{
		return {};
	}

	if (dataSize > 0 && data == nullptr)
	{
		return {};
	}

	const size_t nonceSize = crypto_secretbox_NONCEBYTES;
	const size_t macSize = crypto_secretbox_MACBYTES;
	const size_t size = static_cast<size_t>(dataSize);

	std::vector<std::byte> output(nonceSize + macSize + size);

	randombytes_buf(output.data(), nonceSize);

	if (crypto_secretbox_easy(reinterpret_cast<unsigned char*>(output.data() + nonceSize),
		reinterpret_cast<const unsigned char*>(data), size, reinterpret_cast<const unsigned char*>(output.data()),
		s_defaultKey))
	{
		return {};
	}

	return output;
}

std::vector<std::byte> Encryption::Decrypt(const std::byte* encryptedData, const u32 encryptedSize)
{
	if (!s_sodiumInitialized)
	{
		return {};
	}

	if (encryptedData == nullptr)
	{
		return {};
	}

	const size_t nonceSize = crypto_secretbox_NONCEBYTES;
	const size_t macSize = crypto_secretbox_MACBYTES;
	const size_t size = static_cast<size_t>(encryptedSize);

	if (size < nonceSize + macSize)
	{
		LogColor(LOG_RED, "Ciphertext is too short ", size);
		return {};
	}

	const std::byte* nonce = encryptedData;
	const std::byte* ciphertext = encryptedData + nonceSize;
	const size_t ciphertextSize = size - nonceSize;

	std::vector<std::byte> decrypted(ciphertextSize - macSize);

	if (crypto_secretbox_open_easy(reinterpret_cast<unsigned char*>(decrypted.data()),
		reinterpret_cast<const unsigned char*>(ciphertext), ciphertextSize,
		reinterpret_cast<const unsigned char*>(nonce), s_defaultKey) != 0)
	{
		LogColor(LOG_RED, "Failed to decrypt message (message may be tampered with)");
		return {};
	}

	return decrypted;
}

UUID Encryption::GenerateUUID()
{
	UUID id;
	randombytes_buf(id.data(), id.size());
	return id;
}