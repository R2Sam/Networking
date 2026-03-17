#pragma once

#include "NetworkCore.hpp"
#include "WsNetworkCore.hpp"

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