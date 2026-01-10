#pragma once

#include "NetworkTypes.h"

#include <queue>
#include <unordered_map>

class PeerManager
{
public:

	Peer AddPeer(_ENetPeer* enetPeer, const Address& address, const ConnectionState state);
	Peer AddPeer(const ws_cli_conn_t wsPeer, const Address& address, const ConnectionState state);
	void RemovePeer(const PeerId peerId);

	Peer GetPeer(const PeerId peerId) const;
	Peer GetPeerEnet(const u32 enetPeerId) const;
	Peer GetPeerWs(const ws_cli_conn_t wsPeer) const;

	const std::unordered_map<PeerId, Peer>& GetPeers() const;

private:

	PeerId AllocateId();
	void FreeId(const PeerId peerId);

private:

	PeerId _nextId = 1;
	std::queue<PeerId> _freeIds;

	std::unordered_map<PeerId, Peer> _peers;

	std::unordered_map<u32, PeerId> _enetIds;
	std::unordered_map<ws_cli_conn_t, PeerId> _wsIds;
};