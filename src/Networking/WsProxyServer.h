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

	NetworkCore m_enetCore;
	WsNetworkCore m_wsCore;

	std::unordered_map<PeerId, std::string> m_enetPeers;
	std::unordered_map<PeerId, PeerId> m_wsPeers;
};