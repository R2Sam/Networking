#pragma once

#include "Networking/NetworkTypes.hpp"

template <class Archive>
void serialize(Archive& archive, Address& address) // NOLINT(readability-identifier-naming)
{
	archive(address.ip, address.port);
}

enum class Peer2PeerPacketType : u8
{
	HOST_ID,
	REQUEST,
	ADDRESS,
	HOSTS
};

struct Peer2PeerPacket
{
	Peer2PeerPacketType type;
	PeerId peer;
	Address address;
	std::vector<std::pair<PeerId, Address>> hosts;

	template <class Archive>
	void serialize(Archive& archive) // NOLINT(readability-identifier-naming)
	{
		archive(type, peer, address, hosts);
	}
};