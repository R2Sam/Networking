#include "Encryption.hpp"

#include "Log/Log.hpp"

#include <cstddef>
#include <vector>

static bool s_sodiumInitialized = false;

static constexpr unsigned char DEFAULT_KEY[crypto_secretbox_KEYBYTES] = {0xde, 0xce, 0xfb, 0x88, 0x83, 0xcf, 0xe5, 0x03,
0xd2, 0x8b, 0x62, 0xd3, 0x94, 0x28, 0xcd, 0xd2, 0xaf, 0x82, 0xa7, 0x74, 0x64, 0xcf, 0x0a, 0x1a, 0xff, 0x35, 0xf4, 0x41,
0x13, 0xc8, 0x56, 0xe7};

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