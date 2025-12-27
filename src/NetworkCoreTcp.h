#pragma once

#include "Types.h"

#include "NetworkTypes.h"
#include "PeerManager.h"

#include <queue>

class NetworkCoreTcp
{
public:

	NetworkCoreTcp();
	~NetworkCoreTcp();

	bool InitServer(const u16 port);
	bool InitClient();
	void Shutdown();

	PeerId Connect(const Address& address);
	void Disconnect(const PeerId peer);
	void Poll(std::queue<NetworkEvent>& events, const u32 minTimeMs = 0);

	bool Send(const PeerId peer, const std::vector<u8>& data);

	Peer GetPeer(const PeerId peer);

private:

	bool SendAll(const PeerId peer, const std::vector<u8>& data);

	bool SetSocketNonBlocking(i32 socket);

private:

	i32 _socket = -1;

	PeerManager _peerManager;
};