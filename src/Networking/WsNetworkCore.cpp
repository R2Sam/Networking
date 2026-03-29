#include "WsNetworkCore.hpp"

#include "ws.h"

#include <charconv>
#include <cstring>
#include <thread>

void WsNetworkCore::InitServer(const u16 port)
{
	ws_server server = {.host = "0.0.0.0",
	.port = port,
	.thread_loop = 1,
	.timeout_ms = 5000,
	.evs = {HandleConnect, HandleDisconnect, HandleReceive},
	.context = this};

	ws_socket(&server);
}

void WsNetworkCore::Disconnect(const PeerId peer)
{
	Peer p = m_peerManager.GetPeer(peer);
	if (!p.id)
	{
		return;
	}

	ws_close_client(p.wsPeer);
}

void WsNetworkCore::Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs)
{
	m_eventsMutex.lock();

	if (timeoutMs && m_events.empty())
	{
		m_eventsMutex.unlock();

		std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
		;

		m_eventsMutex.lock();
	}

	events = std::move(m_events);
	m_events = std::queue<NetworkEvent>();

	m_eventsMutex.unlock();
}

bool WsNetworkCore::Send(const PeerId peer, const std::vector<std::byte>& data)
{
	Peer p = m_peerManager.GetPeer(peer);
	if (!p.id)
	{
		return false;
	}

	i32 result = ws_sendframe_bin(p.wsPeer, reinterpret_cast<const char*>(data.data()), data.size());

	return result;
}

bool WsNetworkCore::Send(const PeerId peer, const std::byte* data, const u32 size)
{
	Peer p = m_peerManager.GetPeer(peer);
	if (!p.id)
	{
		return false;
	}

	i32 result = ws_sendframe_bin(p.wsPeer, reinterpret_cast<const char*>(data), size);

	return result;
}

Peer WsNetworkCore::GetPeer(const PeerId peer)
{
	return m_peerManager.GetPeer(peer);
}

void WsNetworkCore::HandleConnect(ws_cli_conn_t client)
{
	WsNetworkCore* context = (WsNetworkCore*)ws_get_server_context(client);

	Peer peer = context->m_peerManager.GetPeerWs(client);

	if (!peer.enetPeer)
	{
		Address address;

		address.ip = ws_getaddress(client);
		char* portString = ws_getport(client);
		std::from_chars(portString, portString + 32, address.port);

		Peer newPeer = context->m_peerManager.AddPeer(client, address, ConnectionState::CONNECTED);

		context->m_eventsMutex.lock();

		context->m_events.emplace(NetworkEventType::CONNECT, newPeer, 0, std::vector<std::byte>());

		context->m_eventsMutex.unlock();
	}

	else
	{
		context->m_events.emplace(NetworkEventType::CONNECT, peer, 0, std::vector<std::byte>());
	}
}

void WsNetworkCore::HandleDisconnect(ws_cli_conn_t client)
{
	WsNetworkCore* context = (WsNetworkCore*)ws_get_server_context(client);

	Peer peer = context->m_peerManager.GetPeerWs(client);

	if (peer.state == ConnectionState::CONNECTED)
	{
		context->m_eventsMutex.lock();

		context->m_events.emplace(NetworkEventType::DISCONNECT, peer, 0, std::vector<std::byte>());

		context->m_eventsMutex.unlock();
	}

	context->m_peerManager.RemovePeer(peer.id);

	context->m_acceptedPeers.erase(peer.id);
}

void WsNetworkCore::HandleReceive(ws_cli_conn_t client, const u8* message, u64 messageSize, i32 type)
{
	if ((type != WS_FR_OP_BIN && type != WS_FR_OP_TXT) || ws_get_state(client) != WS_STATE_OPEN)
	{
		return;
	}

	WsNetworkCore* context = (WsNetworkCore*)ws_get_server_context(client);

	Peer peer = context->m_peerManager.GetPeerWs(client);

	if (context->m_acceptedPeers.find(peer.id) == context->m_acceptedPeers.end())
	{
		if (std::string(message, message + messageSize) != MAGIC_CODE)
		{
			ws_close_client(client);
			return;
		}

		context->m_acceptedPeers.emplace(peer.id);

		return;
	}

	const std::byte* ptr = reinterpret_cast<const std::byte*>(message);

	std::vector<std::byte> data(ptr, ptr + messageSize);

	context->m_eventsMutex.lock();

	context->m_events.emplace(NetworkEventType::RECEIVE, peer, 0, data);

	context->m_eventsMutex.unlock();
}