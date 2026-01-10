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

	Peer Connect(const Address& address, const u32 data = 0);
	void Disconnect(const PeerId peerId, const u32 data = 0);
	void Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs = 0);

	bool Send(const PeerId peerId, const std::vector<u8>& data, const ChannelId channel = 0, const bool reliable = true);
	bool Send(const PeerId peerId, const u8* data, const u32 size, const ChannelId channel = 0, const bool reliable = true);

	Peer GetPeer(const PeerId peerId) const;
	const std::unordered_map<PeerId, Peer>& GetPeers() const;

private:

	void HandleConnect(const _ENetEvent& event, std::queue<NetworkEvent>& events);
	void HandleDisconnect(const _ENetEvent& event, std::queue<NetworkEvent>& events);
	void HandleReceive(const _ENetEvent& event, std::queue<NetworkEvent>& events);

	void AddCompression(_ENetHost* const _host);

private:

	_ENetHost* _host = nullptr;

	PeerManager _peerManager;
};