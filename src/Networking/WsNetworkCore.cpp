#include "WsNetworkCore.h"

#include "ws.h"

#include <charconv>
#include <cstring>
#include <thread>
#include <zlib.h>

void WsNetworkCore::InitServer(const u16 port)
{
	ws_server server =
	{
	.host = "0.0.0.0",
	.port = port,
	.thread_loop = 1,
	.timeout_ms = 5000,
	.evs = {HandleConnect, HandleDisconnect, HandleReceive},
	.context = this};

	ws_socket(&server);
}

void WsNetworkCore::Disconnect(const PeerId peer)
{
	Peer p = _peerManager.GetPeer(peer);
	if (!p.id)
	{
		return;
	}

	ws_close_client(p.wsPeer);
}

void WsNetworkCore::Poll(std::queue<NetworkEvent>& events, const u32 timeoutMs)
{
	_eventsMutex.lock();

	if (timeoutMs && _events.empty())
	{
		_eventsMutex.unlock();

		std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
		;

		_eventsMutex.lock();
	}

	events = std::move(_events);
	_events = std::queue<NetworkEvent>();

	_eventsMutex.unlock();
}

bool WsNetworkCore::Send(const PeerId peer, const std::vector<u8>& data)
{
	Peer p = _peerManager.GetPeer(peer);
	if (!p.id)
	{
		return false;
	}

	i32 result = ws_sendframe_bin(p.wsPeer, (char*)data.data(), data.size());

	return result;
}

bool WsNetworkCore::Send(const PeerId peer, const u8* data, const u32 size)
{
	Peer p = _peerManager.GetPeer(peer);
	if (!p.id)
	{
		return false;
	}

	i32 result = ws_sendframe_bin(p.wsPeer, (char*)data, size);

	return result;
}

Peer WsNetworkCore::GetPeer(const PeerId peer)
{
	return _peerManager.GetPeer(peer);
}

void WsNetworkCore::HandleConnect(ws_cli_conn_t client)
{
	WsNetworkCore* context = (WsNetworkCore*)ws_get_server_context(client);

	Peer peer = context->_peerManager.GetPeerWs(client);

	if (!peer.enetPeer)
	{
		Address address;

		address.ip = ws_getaddress(client);
		char* portString = ws_getport(client);
		std::from_chars(portString, portString + 32, address.port);

		Peer newPeer = context->_peerManager.AddPeer(client, address, ConnectionState::Connected);

		context->_eventsMutex.lock();

		context->_events.emplace(NetworkEventType::Connect, newPeer, 0, std::vector<u8>());

		context->_eventsMutex.unlock();
	}

	else
	{
		context->_events.emplace(NetworkEventType::Connect, peer, 0, std::vector<u8>());
	}
}

void WsNetworkCore::HandleDisconnect(ws_cli_conn_t client)
{
	WsNetworkCore* context = (WsNetworkCore*)ws_get_server_context(client);

	Peer peer = context->_peerManager.GetPeerWs(client);

	if (peer.state == ConnectionState::Connected)
	{
		context->_eventsMutex.lock();

		context->_events.emplace(NetworkEventType::Disconnect, peer, 0, std::vector<u8>());

		context->_eventsMutex.unlock();
	}

	context->_peerManager.RemovePeer(peer.id);

	context->_acceptedPeers.erase(peer.id);
}

void WsNetworkCore::HandleReceive(ws_cli_conn_t client, const u8* message, u64 messageSize, i32 type)
{
	if ((type != WS_FR_OP_BIN && type != WS_FR_OP_TXT) || ws_get_state(client) != WS_STATE_OPEN)
	{
		return;
	}

	WsNetworkCore* context = (WsNetworkCore*)ws_get_server_context(client);

	Peer peer = context->_peerManager.GetPeerWs(client);

	if (context->_acceptedPeers.find(peer.id) == context->_acceptedPeers.end())
	{
		if (std::string(message, message + messageSize) != MagicCode)
		{
			ws_close_client(client);
			return;
		}

		context->_acceptedPeers.emplace(peer.id);

		return;
	}

	std::vector<u8> data(message, message + messageSize);

	context->_eventsMutex.lock();

	context->_events.emplace(NetworkEventType::Receive, peer, 0, data);

	context->_eventsMutex.unlock();
}