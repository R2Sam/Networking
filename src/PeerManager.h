#pragma once

#include "NetworkTypes.h"

#include <queue>
#include <unordered_map>

struct _ENetPeer;

struct Peer
{
	PeerId id = 0;
	_ENetPeer* enetPeer = nullptr;
	Address address;
	ConnectionState state = ConnectionState::Disconnected;
};

class PeerManager
{
public:

	PeerId AddPeer(_ENetPeer* enetPeer, const Address& address, const ConnectionState state);
	void RemovePeer(const PeerId peer);

	Peer GetPeer(const PeerId peer);
	Peer GetPeerEnet(const u32 enetPeerId);

private:

	PeerId AllocateId();
	void FreeId(const PeerId peer);

private:

	PeerId _nextId = 1;
	std::queue<PeerId> _freeIds;

	std::unordered_map<PeerId, Peer> _peers;
	std::unordered_map<u32, PeerId> _ids;
};