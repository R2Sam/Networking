#include "Encryption.h"

#include "Log/Log.h"

#include <vector>

bool sodiumInitialized = false;

const std::string DEFAULT_KEY_HEX = "decefb8883cfe503d28b62d39428cdd2af82a77464cf0a1aff35f44113c856e7";
unsigned char defaultKey[crypto_secretbox_KEYBYTES];

void FromHex(const std::string& hex, unsigned char* key)
{
	for (size_t i = 0; i < hex.length() / 2; ++i)
	{
		std::string byteString = hex.substr(i * 2, 2);
		key[i] = static_cast<unsigned char>(std::stoi(byteString, nullptr, 16));
	}
}

bool Encryption::InitEncryption()
{
	if (!sodiumInitialized)
	{
		if (sodium_init() < 0)
		{
			LogColor(LOG_RED, "Failed to initalize encryption");
			return false;
		}

		for (size_t i = 0; i < DEFAULT_KEY_HEX.length() / 2; ++i)
		{
			std::string byteString = DEFAULT_KEY_HEX.substr(i * 2, 2);
			defaultKey[i] = static_cast<unsigned char>(std::stoi(byteString, nullptr, 16));
		}

		sodiumInitialized = true;
	}

	return true;
}

std::string Encryption::Encrypt(const std::string& data, const std::string& key)
{
	if (!sodiumInitialized)
	{
		return "";
	}

	if (key.size() != crypto_box_BEFORENMBYTES)
	{
		LogColor(LOG_RED, "Bad encryption key");

		return "";
	}

	unsigned char nonce[crypto_secretbox_NONCEBYTES];
	std::vector<unsigned char> ciphertext(crypto_secretbox_MACBYTES + data.size());

	// Generate a random nonce
	randombytes_buf(nonce, sizeof nonce);

	unsigned char keyBuffer[crypto_box_BEFORENMBYTES];
	for (unsigned int i = 0; i < key.size(); i++)
	{
		keyBuffer[i] = (unsigned char)key[i];
	}

	// Encrypt the plaintext
	if (crypto_box_easy_afternm(ciphertext.data(), reinterpret_cast<const unsigned char*>(data.data()), data.size(),
		nonce, keyBuffer))
	{
		LogColor(LOG_RED, "Failed to encrypt with key");

		return "";
	}

	// Create a string to hold nonce and ciphertext
	std::string nonceAndCiphertext(reinterpret_cast<char*>(nonce), sizeof(nonce));
	nonceAndCiphertext.append(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());

	return nonceAndCiphertext;
}

std::string Encryption::Decrypt(const std::string& encryptedData, const std::string& key)
{
	if (!sodiumInitialized)
	{
		return "";
	}

	if (key.size() != crypto_box_BEFORENMBYTES)
	{
		LogColor(LOG_RED, "Bad dencryption key");

		return "";
	}

	// Ensure the message has at least space for nonce and MAC
	if (encryptedData.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
	{
		LogColor(LOG_RED, "Ciphertext is too short ", encryptedData.size());
		return "";
	}

	// Extract nonce and ciphertext
	unsigned char nonce[crypto_secretbox_NONCEBYTES];
	std::copy(encryptedData.begin(), encryptedData.begin() + crypto_secretbox_NONCEBYTES, nonce);

	std::vector<unsigned char> ciphertext(encryptedData.begin() + crypto_secretbox_NONCEBYTES, encryptedData.end());

	// Prepare buffer for decrypted plaintext
	std::vector<unsigned char> decrypted(ciphertext.size() - crypto_secretbox_MACBYTES);

	unsigned char keyBuffer[crypto_box_BEFORENMBYTES];
	for (unsigned int i = 0; i < key.size(); i++)
	{
		keyBuffer[i] = (unsigned char)key[i];
	}

	// Decrypt the ciphertext
	if (crypto_box_open_easy_afternm(decrypted.data(), ciphertext.data(), ciphertext.size(), nonce, keyBuffer) != 0)
	{
		LogColor(LOG_RED, "Failed to decrypt with key");
		return "";
	}

	// Convert decrypted data to a string and return
	return std::string(reinterpret_cast<char*>(decrypted.data()), decrypted.size());
}

std::string Encryption::Encrypt(const std::string& data)
{
	if (!sodiumInitialized)
	{
		return "";
	}

	unsigned char nonce[crypto_secretbox_NONCEBYTES];
	std::vector<unsigned char> ciphertext(crypto_secretbox_MACBYTES + data.size());

	// Generate a random nonce
	randombytes_buf(nonce, sizeof nonce);

	// Encrypt the plaintext
	if (crypto_secretbox_easy(ciphertext.data(), reinterpret_cast<const unsigned char*>(data.data()), data.size(),
		nonce, defaultKey))
	{
		return "";
	}

	// Create a string to hold nonce and ciphertext
	std::string nonceAndCiphertext(reinterpret_cast<char*>(nonce), sizeof(nonce));
	nonceAndCiphertext.append(reinterpret_cast<char*>(ciphertext.data()), ciphertext.size());

	return nonceAndCiphertext;
}

std::string Encryption::Decrypt(const std::string& encryptedData)
{
	if (!sodiumInitialized)
	{
		return "";
	}

	// Ensure the message has at least space for nonce and MAC
	if (encryptedData.size() < crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES)
	{
		LogColor(LOG_RED, "Ciphertext is too short ", encryptedData.size());
		return "";
	}

	// Extract nonce and ciphertext
	unsigned char nonce[crypto_secretbox_NONCEBYTES];
	std::copy(encryptedData.begin(), encryptedData.begin() + crypto_secretbox_NONCEBYTES, nonce);

	std::vector<unsigned char> ciphertext(encryptedData.begin() + crypto_secretbox_NONCEBYTES, encryptedData.end());

	// Prepare buffer for decrypted plaintext
	std::vector<unsigned char> decrypted(ciphertext.size() - crypto_secretbox_MACBYTES);

	// Decrypt the ciphertext
	if (crypto_secretbox_open_easy(decrypted.data(), ciphertext.data(), ciphertext.size(), nonce, defaultKey) != 0)
	{
		LogColor(LOG_RED, "Failed to decrypt message (message may be tampered with)");
		return "";
	}

	// Convert decrypted data to a string and return
	return std::string(reinterpret_cast<char*>(decrypted.data()), decrypted.size());
}

UUID Encryption::GenerateUUID()
{
	UUID id;
	randombytes_buf(id.data(), id.size());
	return id;
}