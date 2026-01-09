#pragma once

#include "NetworkCore.h"

#include "Peer2PeerPacket.h"

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

private:

	NetworkCore _server;

	std::unordered_set<PeerId> _hosts;
};