#pragma once

#include "Types.h"

#include "NetworkTypes.h"
#include "PeerManager.h"

#include <mutex>
#include <queue>
#include <unordered_set>

constexpr std::string MAGIC_CODE = "supersecretcode";

using ws_cli_conn_t = u64;

class WsNetworkCore
{
public:

	void InitServer(const u16 port);

	void Disconnect(const PeerId peer);

	void Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs = 0);

	bool Send(const PeerId peer, const std::vector<u8>& data);
	bool Send(const PeerId peer, const u8* data, const u32 size);

	Peer GetPeer(const PeerId peer);

private:

	static void HandleConnect(ws_cli_conn_t client);
	static void HandleDisconnect(ws_cli_conn_t client);
	static void HandleReceive(ws_cli_conn_t client, const u8* message, u64 messageSize, i32 type);

	std::mutex m_eventsMutex;
	std::queue<NetworkEvent> m_events;

	std::unordered_set<PeerId> m_acceptedPeers;

	PeerManager m_peerManager;
};