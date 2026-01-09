#pragma once

#include "NetworkCore.h"

template<class Archive>
void serialize(Archive& archive, Address& address)
{
	archive(address.ip, address.port);
}

enum class Peer2PeerPacketType
{
	HostId,
	Request,
	Address,
	Hosts
};

struct Peer2PeerPacket
{
	Peer2PeerPacketType type;
	PeerId peer;
	Address address;
	std::vector<std::pair<PeerId, Address>> hosts;

	template<class Archive>
	void serialize(Archive& archive)
	{
		archive(type, peer, address, hosts);
	}
};