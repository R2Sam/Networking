#pragma once

#include "NetworkTypes.h"

#include <queue>
#include <unordered_map>

struct _ENetPeer;

struct Peer
{
	PeerId id = 0;
	_ENetPeer* enetPeer = nullptr;
	i32 socket = -1;
	Address address;
	ConnectionState state = ConnectionState::Disconnected;
};

class PeerManager
{
public:

	PeerId AddPeer(_ENetPeer* enetPeer, const Address& address, const ConnectionState state);
	PeerId AddPeer(const i32 socket, const Address& address, const ConnectionState state);
	void RemovePeer(const PeerId peer);
	bool UpdatePeer(const Peer peer);

	Peer GetPeer(const PeerId peer);
	Peer GetPeerEnet(const u32 enetPeerId);
	Peer GetPeerSocket(const i32 socket);

	std::vector<PeerId> GetAllPeerIds();

	std::queue<std::vector<u8>>& GetSendQueue(const PeerId peer);
	std::vector<u8>& GetRecieveBuffer(const PeerId peer);

private:

	PeerId AllocateId();
	void FreeId(const PeerId peer);

private:

	PeerId _nextId = 1;
	std::queue<PeerId> _freeIds;

	std::unordered_map<PeerId, Peer> _peers;
	std::unordered_map<u32, PeerId> _enetIds;
	std::unordered_map<i32, PeerId> _socketIds;

	std::unordered_map<PeerId, std::queue<std::vector<u8>>> _sendQueues;
	std::unordered_map<PeerId, std::vector<u8>> _receiveBuffers;
};