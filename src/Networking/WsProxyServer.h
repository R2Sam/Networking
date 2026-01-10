#pragma once

#include "NetworkCore.h"
#include "WsNetworkCore.h"

class WsProxyServer
{
public:

	WsProxyServer(const u16 port);

	void Update();

private:

	void HandleEnet(std::queue<NetworkEvent>& events);
	void HandleWs(std::queue<NetworkEvent>& events);

private:

	NetworkCore _enetCore;
	WsNetworkCore _wsCore;

	std::unordered_map<PeerId, std::string> _enetPeers;
	std::unordered_map<PeerId, PeerId> _wsPeers;
};