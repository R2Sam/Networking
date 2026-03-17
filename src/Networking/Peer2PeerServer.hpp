#pragma once

#include "NetworkCore.hpp"

#include "Peer2PeerPacket.hpp"

#include <unordered_set>

class Peer2PeerServer
{
public:

	Peer2PeerServer();

	void Poll(const u32 timeoutMs);

private:

	void HandleHost(const PeerId peer);

	void HandleReceive(const PeerId peer, const Peer2PeerPacket& packet);
	void HandleRequest(const PeerId peer, const PeerId target);
	void HandleList(const PeerId peer);

	NetworkCore m_server;

	std::unordered_set<PeerId> m_hosts;
};