#pragma once

#include "Types.h"

#include "NetworkTypes.h"
#include "PeerManager.h"

#include <queue>


struct _ENetHost;
struct _ENetEvent;

class NetworkCore
{
public:

	NetworkCore();
	~NetworkCore();

	bool InitServer(const u16 port, const u32 maxPeers = 64, const u32 channels = 1);
	bool InitClient(const u32 channels = 1);
	void Shutdown();

	PeerId Connect(const Address& address, const u32 data = 0);
	void Disconnect(const PeerId peer, const u32 data = 0);
	void Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs = 0);

	bool Send(const PeerId peer, const std::vector<u8>& data, const ChannelId channel = 0, const bool reliable = true);

	Peer GetPeer(const PeerId peer);

private:

	void HandleConnect(const _ENetEvent& event, std::queue<NetworkEvent>& events);
	void HandleDisconnect(const _ENetEvent& event, std::queue<NetworkEvent>& events);
	void HandleReceive(const _ENetEvent& event, std::queue<NetworkEvent>& events);

	void AddCompression(_ENetHost* const _host);

private:

	_ENetHost* _host = nullptr;

	PeerManager _peerManager;
};